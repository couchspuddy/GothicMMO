import os

from mcp.server import MCPServer
from openai import AsyncOpenAI

# Orca ADE's OpenAI-compatible endpoint. Port 8000 belongs to unreal-mcp on this
# machine, so the base URL must be set explicitly -- check the Orca dashboard.
ORCA_BASE_URL = os.environ.get("ORCA_BASE_URL", "http://127.0.0.1:11434/v1")
ORCA_MODEL_NAME = os.environ.get("ORCA_MODEL_NAME", "qwen2.5-coder:14b")

app = MCPServer("orca-qwen-bridge", version="0.1.0")
client = AsyncOpenAI(base_url=ORCA_BASE_URL, api_key="not-needed")

SYSTEM_PROMPT = """You are an expert Unreal Engine 5.8 C++ and Python developer assisting with the GothicMMO project.
You prioritize GAS architecture, data-driven design, strict network authority, and narrative-first design."""


@app.tool(
    name="ask_qwen",
    description=(
        "Delegate a C++, Python, or Unreal Engine math/balancing task to the "
        "local Qwen agent via Orca ADE."
    ),
)
async def ask_qwen(task: str, context: str = "") -> str:
    """Ask the local Qwen model a question.

    Args:
        task: The specific task or question for Qwen.
        context: Optional code snippets or file paths to include.
    """
    try:
        response = await client.chat.completions.create(
            model=ORCA_MODEL_NAME,
            messages=[
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": f"Task: {task}\n\nContext:\n{context}"},
            ],
            temperature=0.2,
        )
        return response.choices[0].message.content or ""
    except Exception as e:
        return (
            f"Error reaching Orca ADE at {ORCA_BASE_URL} (model {ORCA_MODEL_NAME}): {e}. "
            "Set ORCA_BASE_URL and ORCA_MODEL_NAME to match the Orca dashboard."
        )


if __name__ == "__main__":
    app.run(transport="stdio")
