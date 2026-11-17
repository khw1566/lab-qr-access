"""안내 음성을 만들어 main/voice/*.pcm (16 kHz mono PCM16LE)으로 저장한다.

표준어 음성(0~3번)은 Google Cloud Text-to-Speech(Chirp3-HD)로 만든다.
사투리 음성(4번 제주, 5번 경상도)은 말투를 문장으로 지시할 수 있는 Gemini TTS로 만든다.
문구도 사투리로 바꿔 쓴다 (DIALECT_LINES).

API 키는 펌웨어에 넣지 않는다 (둘 다 커밋 금지, .gitignore에 있음).
  Cloud TTS: 환경 변수 GOOGLE_TTS_KEY 또는 tools/.google_tts_key
  Gemini:    환경 변수 GEMINI_API_KEY 또는 tools/.gemini_key (https://aistudio.google.com/apikey 무료 키)
문구를 바꾸면 모두 다시 만든다: python tools/gen_voice.py --force
없는 파일만 만든다: python tools/gen_voice.py
음성 종류는 VOICES 순서대로 단말 설정 화면의 음성 번호가 된다 (main/ui_settings.c의 s_voice_names와 같은 순서).
0번은 main/voice/<문구>.pcm, 1번부터는 main/voice/v<번호>_<문구>.pcm 으로 저장한다.
"""
import array
import base64
import io
import json
import math
import os
import re
import sys
import time
import urllib.error
import urllib.request
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEY_FILE = ROOT / 'tools/.google_tts_key'
OUT = ROOT / 'main/voice'
GEMINI_KEY_FILE = ROOT / 'tools/.gemini_key'
GEMINI_MODEL = os.environ.get('GEMINI_TTS_MODEL', 'gemini-3.8-flash-tts')
# main/app.h의 VOICE_COUNT, main/CMakeLists.txt의 EMBED_FILES와 개수를 맞춘다
VOICES = [
    'ko-KR-Chirp3-HD-Aoede',    # 여성 1 (기본)
    'ko-KR-Chirp3-HD-Kore',     # 여성 2
    'ko-KR-Chirp3-HD-Charon',   # 남성 1
    'ko-KR-Chirp3-HD-Puck',     # 남성 2
    'gemini:Sulafat:jeju',      # 제주 사투리 (여성)
    'gemini:Algenib:gyeongsang',  # 경상도 사투리 (남성)
]
RATE = 16000

# 이름은 main/voice.c의 voice_clip_t 순서와 같아야 한다
LINES = {
    'scan': 'QR코드를 사각형 안에 갖다 대 주세요.',
    'checking': '확인하고 있습니다. 잠시만 기다려 주세요.',
    'granted': '출입이 승인되었습니다. 문이 열립니다.',
    'denied': '출입이 거부되었습니다.',
    'offline': '서버에 연결할 수 없습니다. 잠시 후 다시 시도해 주세요.',
}

# 사투리 음성은 문구 자체를 사투리로 바꾸고, Gemini에 억양을 지시한다.
DIALECT_LINES = {
    'jeju': {
        'scan': '혼저 옵서예. QR코드를 네모 안에 대어 줍서.',
        'checking': '확인햄수다. 호꼼만 지드려 줍서.',
        'granted': '출입이 승인되었수다. 문 열렴수다.',
        'denied': '출입이 안 되쿠다.',
        'offline': '서버에 연결이 안 됨수다. 호꼼 이시당 다시 해 봅서.',
    },
    'gyeongsang': {
        'scan': 'QR코드 요 네모 안에 갖다 대 주이소.',
        'checking': '확인하고 있습니더. 쪼매만 기다려 주이소.',
        'granted': '출입 됐습니더. 문 열립니데이.',
        'denied': '출입이 안 됩니더.',
        'offline': '서버에 연결이 안 됩니더. 쪼매 있다가 다시 해 보이소.',
    },
}
# Gemini 3.8 TTS는 본문을 그대로 읽으므로 말투 지시는 speech_metadata.style에 따로 넣는다 (본문에 넣으면 지시문까지 읽는다).
DIALECT_STYLE = {
    'jeju': ('제주도 토박이 중년 여성의 진한 제주 사투리(제주어) 억양. 제주어 특유의 말끝 높낮이를 살리고 '
             '표준어 억양으로 바꾸지 않는다. 따뜻하고 또렷하게, 약간 느긋한 속도.'),
    'gyeongsang': ('부산 토박이 중년 남성의 진한 경상도 사투리 억양. 높낮이(성조)가 뚜렷하고 말끝이 힘 있게 '
                   '떨어진다. 표준어 억양으로 바꾸지 않는다. 친근하고 시원하게, 또렷한 속도.'),
}


def api_key():
    key = os.environ.get('GOOGLE_TTS_KEY') or (KEY_FILE.read_text(encoding='utf-8').strip() if KEY_FILE.exists() else '')
    if not key:
        raise SystemExit('GOOGLE_TTS_KEY 환경 변수나 tools/.google_tts_key 파일이 필요합니다')
    return key


def gemini_key():
    key = os.environ.get('GEMINI_API_KEY') or (GEMINI_KEY_FILE.read_text(encoding='utf-8').strip() if GEMINI_KEY_FILE.exists() else '')
    if not key:
        raise SystemExit('사투리 음성에는 GEMINI_API_KEY 환경 변수나 tools/.gemini_key 파일이 필요합니다 (https://aistudio.google.com/apikey)')
    return key


def resample(pcm, src):
    """PCM16 mono를 src Hz에서 RATE로 바꾼다 (저역 통과 FIR + 선형 보간, 외부 패키지 없이)."""
    if src == RATE:
        return pcm
    samples = array.array('h', pcm)
    if sys.byteorder == 'big':
        samples.byteswap()
    cutoff = 0.45 * RATE / src            # 정규화 차단 주파수 (나이퀴스트 7.2 kHz)
    taps = 63
    mid = taps // 2
    kernel = []
    for i in range(taps):
        n = i - mid
        sinc = 2 * cutoff if n == 0 else math.sin(2 * math.pi * cutoff * n) / (math.pi * n)
        kernel.append(sinc * (0.54 - 0.46 * math.cos(2 * math.pi * i / (taps - 1))))
    gain = sum(kernel)
    kernel = [k / gain for k in kernel]
    padded = [0] * mid + list(samples) + [0] * mid
    count = int(len(samples) * RATE / src)
    out = array.array('h')
    step = src / RATE
    for j in range(count):
        pos = j * step
        base = int(pos)
        frac = pos - base
        acc = 0.0
        for idx in (base, base + 1):
            if idx >= len(samples):
                continue
            weight = (1 - frac) if idx == base else frac
            if weight == 0:
                continue
            window = padded[idx:idx + taps]
            acc += weight * sum(k * v for k, v in zip(kernel, window))
        out.append(max(-32768, min(32767, round(acc))))
    if sys.byteorder == 'big':
        out.byteswap()
    return out.tobytes()


def synthesize_gemini(key, voice, dialect, text):
    body = {'contents': [{'parts': [{'text': text,
                                     'speech_metadata': {'speaker': '안내원', 'style': DIALECT_STYLE[dialect]}}]}],
            'generationConfig': {'responseModalities': ['AUDIO'],
                                 'speechConfig': {'voiceConfig': {'prebuiltVoiceConfig': {'voiceName': voice}}}}}
    request = urllib.request.Request(
        f'https://generativelanguage.googleapis.com/v1beta/models/{GEMINI_MODEL}:generateContent',
        data=json.dumps(body).encode(), headers={'Content-Type': 'application/json', 'x-goog-api-key': key})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                data = json.load(response)
            break
        except urllib.error.HTTPError as error:
            detail = error.read().decode('utf-8', 'replace')[:300]
            if error.code in (429, 500, 503) and attempt < 4:
                time.sleep(15 * (attempt + 1))   # 무료 등급은 분당 요청 수가 적다
                continue
            raise SystemExit(f'Gemini TTS HTTP {error.code}: {detail}') from None
    part = data['candidates'][0]['content']['parts'][0]['inlineData']
    mime = part.get('mimeType', '')
    audio = base64.b64decode(part['data'])
    if 'wav' in mime.lower() or audio[:4] == b'RIFF':
        with wave.open(io.BytesIO(audio), 'rb') as wav:
            if (wav.getnchannels(), wav.getsampwidth()) != (1, 2):
                raise SystemExit('mono PCM16 WAV가 아닌 Gemini 응답')
            return trim(resample(wav.readframes(wav.getnframes()), wav.getframerate()))
    if 'L16' not in mime and 'pcm' not in mime.lower():
        raise SystemExit(f'예상하지 못한 Gemini 오디오 형식: {mime}')
    match = re.search(r'rate=(\d+)', mime)
    return trim(resample(audio, int(match.group(1)) if match else 24000))


def trim(pcm, threshold=300, pad=0.12):
    """앞뒤 무음을 잘라 낸다 (pad초는 남긴다)."""
    samples = array.array('h', pcm)
    if sys.byteorder == 'big':
        samples.byteswap()
    loud = [i for i, v in enumerate(samples) if abs(v) > threshold]
    if not loud:
        return pcm
    keep = int(pad * RATE)
    start, end = max(0, loud[0] - keep), min(len(samples), loud[-1] + keep)
    out = samples[start:end]
    if sys.byteorder == 'big':
        out.byteswap()
    return out.tobytes()


def synthesize(key, voice, text):
    body = {'input': {'text': text}, 'voice': {'languageCode': 'ko-KR', 'name': voice},
            'audioConfig': {'audioEncoding': 'LINEAR16', 'sampleRateHertz': RATE, 'speakingRate': 1.05}}
    request = urllib.request.Request('https://texttospeech.googleapis.com/v1/text:synthesize',
                                     data=json.dumps(body).encode(),
                                     headers={'Content-Type': 'application/json', 'X-Goog-Api-Key': key})
    try:
        with urllib.request.urlopen(request, timeout=45) as response:
            audio = base64.b64decode(json.load(response)['audioContent'])
    except urllib.error.HTTPError as error:
        detail = error.read().decode('utf-8', 'replace')[:300]
        raise SystemExit(f'Google TTS HTTP {error.code}: {detail}') from None
    with wave.open(io.BytesIO(audio), 'rb') as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (RATE, 1, 2):
            raise SystemExit('16 kHz mono PCM16이 아닌 응답')
        return wav.readframes(wav.getnframes())


def main():
    """--force: 모두 다시 만든다. --only 4,5: 그 번호의 음성만 다시 만든다."""
    force = '--force' in sys.argv
    only = None
    if '--only' in sys.argv:
        only = {int(n) for n in sys.argv[sys.argv.index('--only') + 1].split(',')}
        force = True
    key = gkey = None
    OUT.mkdir(parents=True, exist_ok=True)
    for number, voice in enumerate(VOICES):
        if only is not None and number not in only:
            continue
        prefix = f'v{number}_' if number else ''
        dialect = voice.split(':')[2] if voice.startswith('gemini:') else None
        for name, text in (DIALECT_LINES[dialect] if dialect else LINES).items():
            path = OUT / f'{prefix}{name}.pcm'
            if path.exists() and not force:
                continue
            if dialect:
                gkey = gkey or gemini_key()
                pcm = synthesize_gemini(gkey, voice.split(':')[1], dialect, text)
            else:
                key = key or api_key()
                pcm = synthesize(key, voice, text)
            path.write_bytes(pcm)
            print(f'{path.name} ({voice}): {len(pcm) / 2 / RATE:.2f}s  {text}')


if __name__ == '__main__':
    main()
