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
            "Usa # e ## per intestazioni, '-' per liste vere, **parola** per label ovvie, *parola* per parole o frasi da evidenziare.\n"
            "Usa colori inline nel formato {#RRGGBB}parola{/}. Osa il colore per concetti chiave: azzurro #93C5FD (sezioni), verde #22C55E (positivo), rosso #EF4444 (negativo), ambra #F59E0B (warning).\n"
            "Separa bene sezioni e paragrafi con righe vuote per leggibilità su schermo piccolo.\n"
            "Usa solo caratteri ASCII/latin semplici: niente emoji, niente simboli decorativi.\n"
            "Restituisci SOLO il testo finale.\n"
            "Esempi:\n"
            "Buon pomeriggio Simone\n"
            "Mercoledi 4 marzo 2026\n"
            "Meteo Milano\n"
            "Ora: Parzialmente nuvoloso, 14.3 C (percepita simile), umidita 65%, vento 4 km/h\n"
            "Agenda\n"
            "- 18:45-20:00 - Yoga - Hatha (slow) Marghera @ BaliYoga\n"
            "Mercati e Crypto\n"
            "Panoramica: rialzo crypto ancora solido, ma con movimenti un po piu ordinati rispetto ai picchi precedenti\n"
            "BTC ed ETH restano in forte positivo e mantengono la guida del comparto\n"
            "IOTX continua a sovraperformare: buona rotazione sulle alt con sentiment ancora risk-on\n"
            "BTC: $71,124.00 +5.9%\n"
            "ETH: 2219 chars,055.00 +4.6%\n"
            "IOTX: $0.0052 +7.3%\n"
            "Notizie crypto\n"
            "Trend rialzista confermato sui principali, con domanda ancora presente nonostante le oscillazioni\n"
            "IOTX resta tra i token piu forti nel breve, segnale di momentum relativo elevato\n"
            "Notizie\n"
            "Mondo\n"
            "Proseguono gli appelli di Leone XIV alla pace, con tono netto sui conflitti globali\n"
            "Tgcom24 e ANSA rilanciano lo stesso messaggio: \"imploriamo la pace per il mondo intero\"\n"
            "Forte allineamento mediatico sul tema diplomatico, indicatore di fase internazionale delicata\n"
            "Italia\n"
            "Salute: l'orecchio puo diventare una sentinella utile per riconoscere un tipo di ictus\n"
            "In Sardegna prende forma il percorso verso l'Unesco con i dialoghi nuragici in 13 localita\n"
            "Curiosita del giorno\n"
            "Il 4 marzo 1681 Carlo II d'Inghilterra firmo la Carta della Pennsylvania\n"
            "Il territorio fu affidato a William Penn, protagonista della fondazione della colonia\n"
            "Il nome Pennsylvania deriva proprio dal suo fondatore\n"
            "Un atto politico-amministrativo che ha lasciato un segno duraturo nella geografia storica degli Stati Uniti\n"
            "A casa\n"
            "In casa: dato presenza non disponibile nel contesto attuale\n"
            "Temp. esterna: 14.3 C\n"
            "Riscaldamento: probabilmente spento\n"
            "Diventa:\n"
            "# Buon pomeriggio Simone\n"
            "Mercoledi 4 marzo 2026\n"
            "## Meteo Milano\n"
            "- **Ora:** Parzialmente nuvoloso, **14.3 C** (percepita simile), umidita **65%**, vento **4 km/h**\n"
            "## Agenda\n"
            "- **18:45-20:00** - *Yoga* - Hatha (slow) Marghera @ BaliYoga\n"
            "## Mercati e Crypto\n"
            "**Panoramica**: rialzo crypto ancora solido, ma con movimenti un po piu ordinati rispetto ai picchi precedenti\n"
            "*BTC* ed *ETH* restano in forte positivo e mantengono la guida del comparto\n"
            "*IOTX* continua a sovraperformare: buona rotazione sulle alt con sentiment ancora risk-on\n"
            "- **BTC**: $71,124.00 +5.9%\n"
            "- **ETH**: 2219 chars,055.00 +4.6%\n"
            "- **IOTX**: $0.0052 +7.3%\n"
            "## Notizie crypto\n"
            "Trend rialzista confermato sui principali, con domanda ancora presente nonostante le oscillazioni\n"
            "IOTX resta tra i token piu forti nel breve, segnale di momentum relativo elevato\n"
            "## Notizie\n"
            "###Mondo\n"
            "Proseguono gli appelli di Leone XIV alla pace, con tono netto sui conflitti globali\n"
            "Tgcom24 e ANSA rilanciano lo stesso messaggio: \"imploriamo la pace per il mondo intero\"\n"
            "Forte allineamento mediatico sul tema diplomatico, indicatore di fase internazionale delicata\n"
            "### Italia\n"
            "**Salute**: l'orecchio puo diventare una sentinella utile per riconoscere un tipo di ictus\n"
            "In Sardegna prende forma il percorso verso l'Unesco con i dialoghi nuragici in 13 localita\n"
            "## Curiosita del giorno\n"
            "Il **4 marzo 1681** Carlo II d'Inghilterra firmo la *Carta della Pennsylvania*\n"
            "Il territorio fu affidato a *William Penn*, protagonista della fondazione della colonia\n"
            "Il nome *Pennsylvania* deriva proprio dal suo fondatore\n"
            "Un atto politico-amministrativo che ha lasciato un segno duraturo nella geografia storica degli Stati Uniti*\n"
            "## A casa\n"
            "**In casa**: dato presenza non disponibile nel contesto attuale\n"
            "**Temp. esterna**: 14.3 C\n"
            "**Riscaldamento**: probabilmente spento\n\n"
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
