"""Offline tests for SoVITSClient HTTP error handling.

urllib.request.urlopen is monkeypatched so no TTS server is required — only
the HTTPError body reporting and retry policy are exercised.
"""

from __future__ import annotations

import io
import urllib.error

import pytest

from tools import tts_sovits


def _http_error(code: int, body: bytes = b"server diagnosis") -> urllib.error.HTTPError:
    return urllib.error.HTTPError(
        url="http://127.0.0.1:9880/tts", code=code, msg="err", hdrs=None, fp=io.BytesIO(body)
    )


class _FakeResponse:
    def __init__(self, data: bytes):
        self._data = data

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def read(self) -> bytes:
        return self._data


def _client() -> tts_sovits.SoVITSClient:
    # Default text_lang is zh, so the pyopenjtalk pause-insertion path is not hit.
    return tts_sovits.SoVITSClient()


def test_4xx_prints_body_and_does_not_retry(monkeypatch, capsys, tmp_path):
    calls = []

    def fake_urlopen(req, timeout):
        calls.append(req)
        raise _http_error(400, b"cp950 codec error detail")

    monkeypatch.setattr(tts_sovits.urllib.request, "urlopen", fake_urlopen)

    ok = _client().synthesize("test", tmp_path / "out.wav")

    assert ok is False
    assert len(calls) == 1
    out = capsys.readouterr().out
    assert "HTTP 400" in out
    assert "cp950 codec error detail" in out


def test_5xx_retries_once(monkeypatch, tmp_path):
    calls = []

    def fake_urlopen(req, timeout):
        calls.append(req)
        raise _http_error(500)

    monkeypatch.setattr(tts_sovits.urllib.request, "urlopen", fake_urlopen)

    ok = _client().synthesize("test", tmp_path / "out.wav")

    assert ok is False
    assert len(calls) == 2


def test_url_error_retries_once_then_fails(monkeypatch, tmp_path):
    calls = []

    def fake_urlopen(req, timeout):
        calls.append(req)
        raise urllib.error.URLError("connection refused")

    monkeypatch.setattr(tts_sovits.urllib.request, "urlopen", fake_urlopen)

    ok = _client().synthesize("test", tmp_path / "out.wav")

    assert ok is False
    assert len(calls) == 2


def test_success_writes_audio(monkeypatch, tmp_path):
    monkeypatch.setattr(
        tts_sovits.urllib.request, "urlopen", lambda req, timeout: _FakeResponse(b"RIFFdata")
    )

    out_file = tmp_path / "out.wav"
    ok = _client().synthesize("test", out_file)

    assert ok is True
    assert out_file.read_bytes() == b"RIFFdata"
