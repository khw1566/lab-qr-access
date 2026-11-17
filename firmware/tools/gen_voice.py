"""Google Cloud Text-to-Speech로 안내 음성을 만들어 main/voice/*.pcm (16 kHz mono PCM16LE)으로 저장한다.

API 키는 펌웨어에 넣지 않는다. 환경 변수 GOOGLE_TTS_KEY나 tools/.google_tts_key 파일(커밋 금지)에서 읽는다.
문구를 바꾸면 모두 다시 만든다: python tools/gen_voice.py --force
없는 파일만 만든다: python tools/gen_voice.py
음성 종류는 VOICES 순서대로 단말 설정 화면의 음성 번호가 된다 (main/ui_settings.c의 s_voice_names와 같은 순서).
0번은 main/voice/<문구>.pcm, 1번부터는 main/voice/v<번호>_<문구>.pcm 으로 저장한다.
"""
import base64
import io
import json
import os
import urllib.error
import urllib.request
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEY_FILE = ROOT / 'tools/.google_tts_key'
OUT = ROOT / 'main/voice'
# main/app.h의 VOICE_COUNT, main/CMakeLists.txt의 EMBED_FILES와 개수를 맞춘다
VOICES = [
    'ko-KR-Chirp3-HD-Aoede',    # 여성 1 (기본)
    'ko-KR-Chirp3-HD-Kore',     # 여성 2
    'ko-KR-Chirp3-HD-Charon',   # 남성 1
    'ko-KR-Chirp3-HD-Puck',     # 남성 2
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


def api_key():
    key = os.environ.get('GOOGLE_TTS_KEY') or (KEY_FILE.read_text(encoding='utf-8').strip() if KEY_FILE.exists() else '')
    if not key:
        raise SystemExit('GOOGLE_TTS_KEY 환경 변수나 tools/.google_tts_key 파일이 필요합니다')
    return key


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
    import sys
    force = '--force' in sys.argv
    key = None
    OUT.mkdir(parents=True, exist_ok=True)
    for number, voice in enumerate(VOICES):
        prefix = f'v{number}_' if number else ''
        for name, text in LINES.items():
            path = OUT / f'{prefix}{name}.pcm'
            if path.exists() and not force:
                continue
            key = key or api_key()
            pcm = synthesize(key, voice, text)
            path.write_bytes(pcm)
            print(f'{path.name} ({voice}): {len(pcm) / 2 / RATE:.2f}s')


if __name__ == '__main__':
    main()
