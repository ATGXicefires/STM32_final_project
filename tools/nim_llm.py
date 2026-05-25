"""NVIDIA NIM LLM interface for the voice assistant.

Manages chat session history and queries LLM using the OpenAI-compatible SDK.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

# Add parent directory to sys.path to resolve configuration
sys.path.append(str(Path(__file__).resolve().parent.parent))
from tools import config

try:
    from openai import OpenAI
except ImportError:
    OpenAI = None


class NIMLLMEngine:
    """Handles connection to NVIDIA NIM LLM API and maintains conversation history."""

    def __init__(self, api_key: str | None = None) -> None:
        if OpenAI is None:
            raise ImportError("openai package is missing. Please run 'pip install openai'.")

        key = api_key or os.environ.get("NVIDIA_API_KEY") or config.NIM_API_KEY
        if not key or key == "your_nvidia_api_key_here":
            raise ValueError(
                "NVIDIA API Key is missing. Please set NVIDIA_API_KEY in a .env file "
                "in the project root directory, or set the environment variable."
            )
        self.client = OpenAI(base_url=config.NIM_BASE_URL, api_key=key)
        self.model = config.NIM_MODEL
        self.system_prompt = config.SYSTEM_PROMPT
        self.history: list[dict[str, str]] = []

    def get_response(self, user_text: str) -> str:
        """Sends the user's message to the LLM and returns the assistant's reply.

        Appends the interaction to the session history.
        """
        messages = [{"role": "system", "content": self.system_prompt}]
        messages.extend(self.history)
        messages.append({"role": "user", "content": user_text})

        completion = self.client.chat.completions.create(
            model=self.model,
            messages=messages,
            temperature=0.7,
            max_tokens=150,
        )

        reply = completion.choices[0].message.content
        if reply is None:
            reply = ""
        reply = reply.strip()

        # Update local session memory
        self.history.append({"role": "user", "content": user_text})
        self.history.append({"role": "assistant", "content": reply})

        return reply

    def reset_session(self) -> None:
        """Resets the conversation history."""
        self.history.clear()


# Singleton LLM instance
_engine: NIMLLMEngine | None = None


def get_llm_engine(api_key: str | None = None) -> NIMLLMEngine:
    """Retrieves or instantiates the global LLM engine singleton."""
    global _engine
    if _engine is None:
        _engine = NIMLLMEngine(api_key=api_key)
    return _engine


def get_response(user_text: str, api_key: str | None = None) -> str:
    """Helper function to query the LLM using the shared singleton engine."""
    engine = get_llm_engine(api_key=api_key)
    return engine.get_response(user_text)


def reset_session() -> None:
    """Helper function to reset the shared singleton engine's chat session."""
    engine = get_llm_engine()
    engine.reset_session()
