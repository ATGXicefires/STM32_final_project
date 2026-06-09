"""Offline tests for NIMLLMEngine history trimming.

The OpenAI client is replaced with a fake returning canned replies, so no
network or API key is required — only the history-cap logic is exercised.
"""

from __future__ import annotations

from types import SimpleNamespace

from tools import nim_llm


class _FakeCompletions:
    def create(self, **kwargs):
        return SimpleNamespace(
            choices=[SimpleNamespace(message=SimpleNamespace(content="ok"))]
        )


class _FakeClient:
    def __init__(self):
        self.chat = SimpleNamespace(completions=_FakeCompletions())


def test_history_is_capped_to_max_turns():
    engine = nim_llm.NIMLLMEngine(api_key="test-key")
    engine.client = _FakeClient()

    for i in range(nim_llm.HISTORY_MAX_TURNS + 5):
        engine.get_response(f"msg {i}")

    assert len(engine.history) == nim_llm.HISTORY_MAX_TURNS * 2
    # The oldest turns are dropped; the most recent user message is kept.
    assert engine.history[0] == {"role": "user", "content": "msg 5"}
    assert engine.history[-2] == {
        "role": "user",
        "content": f"msg {nim_llm.HISTORY_MAX_TURNS + 4}",
    }


def test_history_below_cap_is_untouched():
    engine = nim_llm.NIMLLMEngine(api_key="test-key")
    engine.client = _FakeClient()

    for i in range(3):
        engine.get_response(f"msg {i}")

    assert len(engine.history) == 6
    assert engine.history[0] == {"role": "user", "content": "msg 0"}
