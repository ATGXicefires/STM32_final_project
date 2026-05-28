"""Offline tests for NIMLLMEngine.get_response_stream.

The OpenAI client is replaced with a fake that yields canned chunks, so no
network or API key is required — only the concat / history logic is exercised.
"""

from __future__ import annotations

from types import SimpleNamespace

from tools import nim_llm


def _chunk(content: str | None):
    """Builds a fake stream chunk shaped like an OpenAI streaming delta."""
    return SimpleNamespace(choices=[SimpleNamespace(delta=SimpleNamespace(content=content))])


class _FakeCompletions:
    def __init__(self, chunks):
        self._chunks = chunks

    def create(self, **kwargs):
        assert kwargs.get("stream") is True
        return iter(self._chunks)


class _FakeClient:
    def __init__(self, chunks):
        self.chat = SimpleNamespace(completions=_FakeCompletions(chunks))


def test_get_response_stream_concats_and_updates_history():
    # api_key is a non-placeholder string so __init__ passes validation; OpenAI()
    # construction makes no network call. We then swap the client for the fake.
    engine = nim_llm.NIMLLMEngine(api_key="test-key")
    engine.client = _FakeClient([_chunk("你好"), _chunk("，"), _chunk(None), _chunk("世界。")])

    out = list(engine.get_response_stream("hi"))

    assert "".join(out) == "你好，世界。"
    assert engine.history[-2] == {"role": "user", "content": "hi"}
    assert engine.history[-1] == {"role": "assistant", "content": "你好，世界。"}


def test_get_response_stream_skips_empty_deltas():
    engine = nim_llm.NIMLLMEngine(api_key="test-key")
    engine.client = _FakeClient([_chunk(None), _chunk(""), _chunk("a"), _chunk(None)])

    out = list(engine.get_response_stream("hi"))

    assert out == ["a"]
    assert engine.history[-1] == {"role": "assistant", "content": "a"}
