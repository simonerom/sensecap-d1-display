#!/usr/bin/env python3
import json
import os
import subprocess
import sys
from pathlib import Path

NODE = "/Users/simone/.nvm/versions/node/v23.3.0/bin/node"
OPENCLAW = "/opt/homebrew/bin/openclaw"
CRON_JOBS = Path('/Users/simone/.openclaw/cron/jobs.json')


def run_agent(prompt: str, timeout: int = 120) -> str:
    agent_id = os.getenv("HOME_MESSAGE_AGENT_ID", "homegen")
    cmd = [NODE, OPENCLAW, "agent", "--agent", agent_id, "--json", "--message", prompt, "--timeout", str(timeout)]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 10)
    if p.returncode != 0:
        raise RuntimeError((p.stderr or p.stdout or "agent failed").strip())
    data = json.loads(p.stdout)
    payloads = (((data or {}).get("result") or {}).get("payloads") or [])
    if not payloads:
        raise RuntimeError("no payloads in agent response")
    text = (payloads[0].get("text") or "").strip()
    if not text:
        raise RuntimeError("empty text payload")
    return text


def get_buongiorno_template() -> str:
    try:
        j = json.loads(CRON_JOBS.read_text())
        for job in j.get("jobs", []):
            if job.get("name") == "buongiorno":
                return (((job.get("payload") or {}).get("message") or "").strip())
    except Exception:
        pass
    return ""


def main():
    raw = sys.stdin.read()
    payload = json.loads(raw) if raw.strip() else {}

    task = payload.get("task")
    if task == "format_home_message_markdown_lite":
        text = payload.get("input", "")
        prompt = (
            "Riformatta il testo seguente per home dashboard in markdown-lite.\n"
            "Pipeline: il testo è già de-emoji, NON reintrodurre emoji.\n"
            "Regole obbligatorie: mantieni i fatti, niente invenzioni, niente tabelle, niente code block, italiano naturale.\n"
            "Usa # e ## per intestazioni, '-' per liste vere, **parola** per label ovvie (es: Oggi, Domani, BTC, ETH, IOTX, Agenda, Notizie).\n"
            "Usa colori inline nel formato {#RRGGBB}parola{/}. Osa il colore per concetti chiave: azzurro #93C5FD (sezioni), verde #22C55E (positivo), rosso #EF4444 (negativo), ambra #F59E0B (warning).\n"
            "Separa bene sezioni e paragrafi con righe vuote per leggibilità su schermo piccolo.\n"
            "Usa solo caratteri ASCII/latin semplici: niente emoji, niente simboli decorativi.\n"
            "Restituisci SOLO il testo finale.\n\n"
            f"TESTO:\n{text}"
        )
        out = run_agent(prompt, timeout=90)
        print(out)
        return

    base = get_buongiorno_template()
    data_block = json.dumps(payload, ensure_ascii=False)
    prompt = (
        "Usa il seguente job 'buongiorno' come base di tono e contenuto, ma NON inviare messaggi e NON usare tool di delivery.\n"
        "Obiettivo: genera SOLO il testo per home dashboard in markdown-lite (max 2600 caratteri).\n"
        "Formato consentito: #, ##, ###, **bold**, *italic*, - bullet.\n"
        "Compatto ma ricco, italiano naturale.\n\n"
        f"JOB_ORIGINALE:\n{base}\n\n"
        f"DATI_CONTESTO:\n{data_block}\n\n"
        "Restituisci SOLO il testo finale."
    )
    out = run_agent(prompt, timeout=180)
    print(out)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)
