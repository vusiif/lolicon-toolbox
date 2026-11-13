"""Shared pieces of the lossy GBK repair harness: the C++ tools, the corpus,
an OpenAI-compatible client, and scoring.

The original text is read only by prepare() and score(); nothing derived from
it reaches a request. The API key comes from the MIMO_API_KEY environment
variable and is never logged; the address and model default to the app's own
settings (HKCU\\Software\\Toolbox\\Toolbox\\AI), which hold no key.
"""
import concurrent.futures
import hashlib
import http.client
import json
import os
import pathlib
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = ROOT / "cmake-build-release" / "tests"


# ------------------------------------------------------------------ C++ tools
def tool(*args, exe="LossyGbkTool", ok=(0, 1)) -> str:
    done = subprocess.run([str(BUILD / f"{exe}.exe"), *map(str, args)], capture_output=True)
    text = done.stdout.decode("utf-8")
    if done.returncode not in ok:
        sys.exit(f"{exe} {args[0]} failed: {text.strip()}")
    return text


def jsonl(text: str) -> list:
    return [json.loads(line) for line in text.splitlines() if line.strip()]


# ---------------------------------------------------------------------- corpus
def encodable(text: str) -> str:
    """Characters GBK cannot encode become '?', as the corpus generator's encode_errors=replace does."""
    out = []
    for c in text:
        try:
            c.encode("gbk")
            out.append(c)
        except UnicodeEncodeError:
            out.append("?")
    return "".join(out)


def prepare(doc: pathlib.Path, work: pathlib.Path) -> dict:
    """Damage a corpus document per byte. Returns paths and the spans (units of at most 8 characters)."""
    work.mkdir(parents=True, exist_ok=True)
    with open(doc / "ground_truth.txt", encoding="utf-8-sig", newline="") as f:
        truth = encodable(f.read())
    (work / "truth.txt").write_text(truth, encoding="utf-8", newline="")
    tool("damage", work / "truth.txt", work / "damaged.txt")
    if tool("validate", work / "truth.txt", work / "damaged.txt").strip() not in ("consistent (per-byte)", "consistent (either replacement)"):
        sys.exit(f"{doc.name}: the original does not reproduce its damage")
    units = jsonl(tool("candidates", work / "damaged.txt", 0, 1, 8))  # limit 0: the spans only
    d = draft(work / "damaged.txt")
    if len(d["items"]) != len(truth) or any(d["unit_of"][i] != u["id"] for u in units for i in range(u["begin"], u["end"])):
        sys.exit(f"{doc.name}: the draft does not line up with the spans")
    return {"name": doc.name, "work": work, "damaged": work / "damaged.txt", "truth_path": work / "truth.txt", "units": units}


def draft(damaged: pathlib.Path) -> dict:
    """The local draft: glossary and the marked text split into items, one per character."""
    text = tool("draft", damaged)
    head, _, body = text.partition("\n\n")
    glossary_line = next((l for l in head.splitlines() if l.startswith("glossary: ")), "glossary: ")
    glossary = [w for w in glossary_line[len("glossary: "):].split("、") if w]
    body = body[:-1] if body.endswith("\n") else body  # the tool's own line end; the text may end with one too
    items, unit_of = [], []  # items[i]: markup of character i; unit_of[i]: its span id or 0
    current, i = 0, 0
    while i < len(body):
        c = body[i]
        if c == "⟦":
            colon = body.index(":", i)
            current = int(body[i + 1:colon])
            i = colon + 1
            continue
        if c == "⟧":
            current = 0
            i += 1
            continue
        if c == "〔":
            end = body.index("〕", i)
            items.append(body[i:end + 1])
            i = end + 1
        else:
            items.append(c)
            i += 1
        unit_of.append(current)
    return {"glossary": glossary, "items": items, "unit_of": unit_of}


# ---------------------------------------------------------------------- model
def environment(name: str):
    """An environment variable, also when it was set (user or system wide) after this process started."""
    if os.environ.get(name):
        return os.environ[name]
    try:
        import winreg
        for root, path in ((winreg.HKEY_CURRENT_USER, r"Environment"),
                           (winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment")):
            try:
                with winreg.OpenKey(root, path) as key:
                    if value := winreg.QueryValueEx(key, name)[0]:
                        return value
            except OSError:
                pass
    except ImportError:
        pass
    return None


def app_settings() -> tuple:
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Toolbox\Toolbox\AI") as key:
            return winreg.QueryValueEx(key, "baseUrl")[0], winreg.QueryValueEx(key, "model")[0]
    except OSError:
        return None, None


class Stalled(Exception):
    pass


class Client:
    """Chat completions as the app sends them: streamed, no temperature.

    thinking=False turns MiMo and DeepSeek thinking off, as the app does.
    thinking=True asks for it explicitly and allows max_tokens for thinking and
    answer together (MiMo counts both in max_completion_tokens). Thinking
    models ignore temperature, so replies vary: sample=n caches the n-th
    sample of the same request separately.

    A stream that sends nothing for `stall` seconds, or runs past `deadline`,
    is abandoned; progress (thinking and answer characters) goes to stderr
    every `report` seconds so a slow reply can be told from a dead one.
    """

    def __init__(self, base_url=None, model=None, cache=None, thinking=True, max_tokens=32768,
                 stall=120, deadline=1200, report=30, key_file=None, key_env=None, effort=None):
        app_base, app_model = app_settings()
        self.base_url = (base_url or os.environ.get("MIMO_BASE_URL") or app_base or "").rstrip("/")
        self.model = model or os.environ.get("MIMO_MODEL") or app_model
        host = re.sub(r"^https?://([^/:]+).*$", r"\1", self.base_url).lower()
        # The key: a file's first line, else an environment variable named for the service.
        key_env = key_env or ("DEEPSEEK_API_KEY" if host == "api.deepseek.com" else "MIMO_API_KEY")
        self.key = pathlib.Path(key_file).read_text(encoding="utf-8-sig").splitlines()[0].strip() if key_file else environment(key_env)
        if not self.base_url or not self.model or not self.key:
            sys.exit(f"Set {key_env} or pass --key-file (and MIMO_BASE_URL / MIMO_MODEL unless the app has them).")
        self.cache = pathlib.Path(cache) if cache else None
        self.deepseek = host == "api.deepseek.com"
        self.switchable = host.endswith("xiaomimimo.com") or self.deepseek
        self.thinking, self.max_tokens, self.effort = thinking, max_tokens, effort
        self.stall, self.deadline, self.report = stall, deadline, report

    def body(self, messages, **extra) -> dict:
        body = {"model": self.model, "messages": messages, **extra}
        if self.switchable:
            body["thinking"] = {"type": "enabled" if self.thinking else "disabled"}
        if self.thinking:
            # MiMo documents max_completion_tokens. DeepSeek has no such field (a 5-request probe wrote 249k
            # tokens): its max_tokens defaults to 64K when thinking, and reasoning_effort (low, high, max;
            # default high) sets how hard it thinks.
            body["max_tokens" if self.deepseek else "max_completion_tokens"] = self.max_tokens
            if self.effort:
                body["reasoning_effort"] = self.effort
        return body

    def chat(self, messages, label="", sample=0, **extra) -> dict:
        """Returns {content, reasoning, finish_reason, usage, ms, first_ms, cached, error}."""
        body = self.body(messages, **extra)
        # The cache key leaves the transport out (stream), so replies from earlier unstreamed runs still count.
        keyed = {"model": body["model"], "messages": body["messages"], "stream": False,
                 **{k: v for k, v in body.items() if k not in ("model", "messages")}}  # field order of the earlier body
        key = json.dumps(keyed, ensure_ascii=False) + (f"#sample{sample}" if sample else "")
        path = self.cache / f"{hashlib.sha256(key.encode('utf-8')).hexdigest()}.json" if self.cache else None
        if path and path.exists():
            return {**json.loads(path.read_text(encoding="utf-8")), "cached": True}
        url = self.base_url if self.base_url.endswith("/chat/completions") else self.base_url + "/chat/completions"
        raw = json.dumps({**body, "stream": True, "stream_options": {"include_usage": True}}, ensure_ascii=False).encode("utf-8")
        error = None
        for attempt in range(3):
            request = urllib.request.Request(url, data=raw, method="POST", headers={
                "Content-Type": "application/json", "Accept": "text/event-stream", "Authorization": f"Bearer {self.key}"})
            try:
                result = self._stream(request, label)
                if result["finish_reason"] is None and not result["content"]:
                    raise Stalled("stream ended without an answer")
                if path:
                    self.cache.mkdir(parents=True, exist_ok=True)
                    path.write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
                return {**result, "cached": False}
            except urllib.error.HTTPError as e:
                error = f"HTTP {e.code}"
                if e.code < 500 and e.code != 429:
                    print(f"[{label}] {error}: not retried", file=sys.stderr, flush=True)
                    break
                time.sleep(10 * (attempt + 1) if e.code == 429 else 2)
            except Stalled as e:
                error = f"stalled: {e}"
            except (urllib.error.URLError, http.client.HTTPException, OSError, KeyError, ValueError) as e:
                error = type(e).__name__
                time.sleep(2 * (attempt + 1))
            print(f"[{label}] attempt {attempt + 1}: {error}", file=sys.stderr, flush=True)
        return {"content": "", "reasoning": "", "finish_reason": None, "usage": {}, "ms": 0, "first_ms": None, "error": error, "cached": False}

    def _stream(self, request, label) -> dict:
        start = time.monotonic()
        content, reasoning, finish, usage, first, last_report = [], [], None, {}, None, start
        # The socket timeout bounds each read: a silent stream raises after `stall` seconds.
        with urllib.request.urlopen(request, timeout=self.stall) as reply:
            if "event-stream" not in (reply.headers.get("Content-Type") or ""):
                data = json.loads(reply.read().decode("utf-8"))  # a server that ignores stream
                message = data["choices"][0]["message"]
                return {"content": message.get("content") or "", "reasoning": message.get("reasoning_content") or "",
                        "finish_reason": data["choices"][0].get("finish_reason"), "usage": data.get("usage") or {},
                        "ms": int(1000 * (time.monotonic() - start)), "first_ms": None, "error": None}
            while True:
                try:
                    line = reply.readline()
                except TimeoutError as e:
                    raise Stalled(f"no data for {self.stall} s") from e
                now = time.monotonic()
                if now - start > self.deadline:
                    raise Stalled(f"past the {self.deadline} s deadline")
                if self.report and now - last_report >= self.report:
                    print(f"[{label}] {int(now - start)} s: thinking {sum(map(len, reasoning))} chars, answer {sum(map(len, content))} chars",
                          file=sys.stderr, flush=True)
                    last_report = now
                if not line:
                    break
                line = line.decode("utf-8").strip()
                if not line.startswith("data:"):
                    continue
                data = line[5:].strip()
                if data == "[DONE]":
                    break
                event = json.loads(data)
                if event.get("usage"):
                    usage = event["usage"]
                for choice in event.get("choices") or []:
                    delta = choice.get("delta") or {}
                    if delta.get("reasoning_content") or delta.get("content"):
                        first = first or int(1000 * (now - start))
                    reasoning.append(delta.get("reasoning_content") or "")
                    content.append(delta.get("content") or "")
                    finish = choice.get("finish_reason") or finish
        return {"content": "".join(content), "reasoning": "".join(reasoning), "finish_reason": finish, "usage": usage,
                "ms": int(1000 * (time.monotonic() - start)), "first_ms": first, "error": None}


def parse_fills(content: str):
    """The fills object of a reply, or None when the reply is not the required JSON."""
    start, end = content.find("{"), content.rfind("}")
    if start < 0 or end < start:
        return None
    try:
        reply = json.loads(content[start:end + 1])
    except ValueError:
        return None
    fills = reply.get("fills") if isinstance(reply, dict) else None
    return {str(k): v for k, v in fills.items()} if isinstance(fills, dict) else None


def parse_candidates(content: str):
    """The candidates object of an e2 reply (id -> list of fills), or None."""
    start, end = content.find("{"), content.rfind("}")
    if start < 0 or end < start:
        return None
    try:
        reply = json.loads(content[start:end + 1])
    except ValueError:
        return None
    got = reply.get("candidates") if isinstance(reply, dict) else None
    if not isinstance(got, dict):
        return None
    return {str(k): [t for t in v if isinstance(t, str)] if isinstance(v, list) else [v] if isinstance(v, str) else []
            for k, v in got.items()}


def parse_picks(content: str):
    """The picks object of an e3 reply (slot -> one character), or None."""
    start, end = content.find("{"), content.rfind("}")
    if start < 0 or end < start:
        return None
    try:
        reply = json.loads(content[start:end + 1])
    except ValueError:
        return None
    got = reply.get("picks") if isinstance(reply, dict) else None
    return {str(k): v for k, v in got.items() if isinstance(v, str) and len(v) == 1} if isinstance(got, dict) else None


def parse_alternatives(content: str):
    """The picks object of an e4 reply (slot -> characters, likeliest first), or None."""
    start, end = content.find("{"), content.rfind("}")
    if start < 0 or end < start:
        return None
    try:
        reply = json.loads(content[start:end + 1])
    except ValueError:
        return None
    got = reply.get("picks") if isinstance(reply, dict) else None
    if not isinstance(got, dict):
        return None
    out = {}
    for k, v in got.items():
        values = v if isinstance(v, list) else [v]
        chars = [c for c in values if isinstance(c, str) and len(c) == 1 and c != "□"]
        if chars:
            out[str(k)] = list(dict.fromkeys(chars))
    return out


def run_all(jobs, work, parallel):
    with concurrent.futures.ThreadPoolExecutor(parallel) as pool:
        return list(pool.map(work, jobs))


# ------------------------------------------------------------------- scoring
def read(path) -> str:
    """Text as the C++ tools see it: line ends kept, BOM dropped (positions must line up)."""
    with open(path, encoding="utf-8-sig", newline="") as f:
        return f.read()


def score(prepared: dict, fills: dict, out: pathlib.Path) -> dict:
    """fills: global span id (int) -> text. Splices them in with LossyGbkTool apply
    (guided() re-solves against the bytes) and scores raw fills and the result."""
    truth = read(prepared["truth_path"])
    (out / "fills.json").write_text(json.dumps({str(k): v for k, v in fills.items()}, ensure_ascii=False), encoding="utf-8")
    apply_log = tool("apply", prepared["damaged"], out / "fills.json", out / "repaired.txt")
    (out / "apply.log").write_text(apply_log, encoding="utf-8")
    repaired = read(out / "repaired.txt")
    if len(repaired) != len(truth):
        sys.exit(f"{prepared['name']}: repaired text does not line up with the original")
    m = dict.fromkeys(["partial", "partial_right", "lost", "lost_right", "raw_partial", "raw_partial_right",
                       "raw_lost", "raw_lost_right", "spans", "spans_right", "answered", "wrong_length"], 0)
    lossy = set()
    for u in prepared["units"]:
        b, e, ev = u["begin"], u["end"], u["evidence"]
        fill = fills.get(u["id"])
        m["spans"] += 1
        m["spans_right"] += repaired[b:e] == truth[b:e]
        if isinstance(fill, str) and fill:
            m["answered"] += 1
            if len(fill) != e - b:
                m["wrong_length"] += 1
                fill = None
        else:
            fill = None
        for i in range(b, e):
            lossy.add(i)
            kind = "partial" if ev[i - b] == "P" else "lost"
            m[kind] += 1
            m[f"{kind}_right"] += repaired[i] == truth[i]
            if fill is not None:
                m[f"raw_{kind}"] += 1
                m[f"raw_{kind}_right"] += fill[i - b] == truth[i]
    m["certain_changed"] = sum(1 for i in range(len(truth)) if i not in lossy and repaired[i] != truth[i])
    m["all"], m["all_right"] = len(truth), sum(a == b for a, b in zip(repaired, truth))
    return m


def pct(a, b) -> str:
    return f"{100 * a / b:5.1f}%" if b else "  n/a "


def summary(m: dict) -> str:
    return (f"all {pct(m['all_right'], m['all'])}  partial {pct(m['partial_right'], m['partial'])}  "
            f"lost {pct(m['lost_right'], m['lost'])}  spans right {pct(m['spans_right'], m['spans'])}  "
            f"| raw fills: partial {pct(m['raw_partial_right'], m['raw_partial'])} lost {pct(m['raw_lost_right'], m['raw_lost'])}  "
            f"answered {m['answered']}/{m['spans']} wrong length {m['wrong_length']}  certain changed {m['certain_changed']}")
