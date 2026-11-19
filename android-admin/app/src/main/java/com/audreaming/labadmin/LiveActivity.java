package com.audreaming.labadmin;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.concurrent.LinkedBlockingQueue;

import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;
import okio.ByteString;

/**
 * 단말 실시간 보기. 형식은 server/src/live.js 머리말과 같다.
 * 0x01 JPEG(단말→관리자), 0x02 마이크 PCM(단말→관리자), 0x03 말하기 PCM(관리자→단말). 음성은 s16le 8 kHz mono.
 * 영상·음성은 저장하지 않는다.
 */
public class LiveActivity extends Activity {
    private static final int RATE = 8000;
    private static final int TALK_CHUNK = 2048; // 서버 제한 4096보다 작게
    private static final long NO_DEVICE_MS = 30_000;
    private static final int REQ_MIC = 7;

    private WebSocket ws;
    private ImageView img;
    private TextView msg, badge, status;
    private Button listenBtn, talkBtn;
    private boolean closed, online, ended, listening, micAllowed;
    private volatile boolean talking;
    private AudioTrack track;
    private Thread playThread, talkThread;
    private final LinkedBlockingQueue<byte[]> playQueue = new LinkedBlockingQueue<>(50);
    private int frames;
    private long fpsAt;

    private final Runnable noDevice = () -> { if (!online && !closed) show("단말이 응답하지 않습니다. 단말 전원과 Wi-Fi 연결, 펌웨어 버전을 확인하세요."); };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        micAllowed = getIntent().getBooleanExtra("mic", false);
        buildUi(getIntent().getStringExtra("name"));
        connect(getIntent().getIntExtra("id", 0));
    }

    private void buildUi(String name) {
        LinearLayout root = Ui.vbox(this);
        root.setBackgroundColor(Ui.BG);
        int p = Ui.dp(this, 16);

        LinearLayout head = Ui.hbox(this);
        head.addView(Ui.bold(this, name + " 실시간 보기", 18), new LinearLayout.LayoutParams(0, -2, 1));
        head.addView(Ui.smallButton(this, "닫기", Ui.SECONDARY, v -> finish()));
        root.addView(head);

        FrameLayout screen = new FrameLayout(this);
        screen.setBackground(Ui.box(0xFF0B0F17, 0, 10, this));
        screen.setClipToOutline(true);
        img = new ImageView(this);
        img.setScaleType(ImageView.ScaleType.FIT_CENTER);
        img.setVisibility(View.GONE);
        screen.addView(img, new FrameLayout.LayoutParams(-1, -1));
        msg = Ui.text(this, "단말에 연결하는 중…", 14, 0xFFCBD5E1);
        msg.setGravity(Gravity.CENTER);
        msg.setPadding(p, p, p, p);
        screen.addView(msg, new FrameLayout.LayoutParams(-1, -1));
        badge = Ui.pill(this, "LIVE", Color.WHITE, 0xE6B91C1C);
        badge.setVisibility(View.GONE);
        FrameLayout.LayoutParams bp = new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.START);
        bp.setMargins(Ui.dp(this, 10), Ui.dp(this, 10), 0, 0);
        screen.addView(badge, bp);
        int w = getResources().getDisplayMetrics().widthPixels - 2 * p;
        LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(-1, w * 3 / 4); // 단말 카메라는 4:3
        sp.topMargin = Ui.dp(this, 12);
        root.addView(screen, sp);

        FlowLayout bar = Ui.flow(this);
        listenBtn = Ui.smallButton(this, "단말 소리 듣기", Ui.PRIMARY, v -> setListen(!listening));
        listenBtn.setEnabled(micAllowed);
        talkBtn = Ui.smallButton(this, "말하기", Ui.PRIMARY, v -> toggleTalk());
        bar.addView(listenBtn);
        bar.addView(talkBtn);
        Spinner fps = Ui.spinner(this, MainActivity.labels("2 fps", "5 fps", "10 fps"), 1);
        fps.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            boolean first = true;
            public void onItemSelected(AdapterView<?> pa, View v, int pos, long id) {
                if (first) { first = false; return; }
                sendJson("{\"t\":\"fps\",\"v\":" + new int[]{2, 5, 10}[pos] + "}");
            }
            public void onNothingSelected(AdapterView<?> pa) { }
        });
        bar.addView(fps);
        status = Ui.small(this, "");
        bar.addView(status);
        Ui.add(root, bar, 12);

        Ui.add(root, Ui.small(this, "영상과 음성은 저장하지 않습니다. 연결하는 동안 단말 화면에 관리자 연결 중 표시가 나오고, 누가 언제 연결했는지만 기록에 남습니다. 한 번에 최대 10분까지 연결됩니다."
                + (micAllowed ? "" : " 단말 소리 듣기는 기능 설정에서 켤 수 있습니다.")), 12);

        root.setOnApplyWindowInsetsListener((v, insets) -> {
            android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars());
            v.setPadding(p + i.left, p + i.top, p + i.right, p + i.bottom);
            return WindowInsets.CONSUMED;
        });
        setContentView(Ui.scroll(root));
    }

    private void show(String text) {
        msg.setText(text);
        msg.setVisibility(text == null || text.isEmpty() ? View.GONE : View.VISIBLE);
    }

    private void connect(int id) {
        Ui.later(noDevice, NO_DEVICE_MS);
        fpsAt = System.currentTimeMillis();
        ws = Api.get(this).openLive(id, new WebSocketListener() {
            @Override public void onMessage(WebSocket s, String text) { Ui.main(() -> onText(text)); }

            @Override public void onMessage(WebSocket s, ByteString bytes) {
                if (bytes.size() < 2) return;
                int kind = bytes.getByte(0) & 0xFF;
                if (kind == 1) {
                    byte[] jpg = bytes.substring(1).toByteArray();
                    Bitmap bm = BitmapFactory.decodeByteArray(jpg, 0, jpg.length);
                    if (bm != null) Ui.main(() -> onFrame(bm));
                } else if (kind == 2 && listening) {
                    playQueue.offer(bytes.substring(1).toByteArray());
                }
            }

            @Override public void onFailure(WebSocket s, Throwable t, Response r) {
                String why = r != null && r.code() >= 400 ? "연결하지 못했습니다 (HTTP " + r.code() + "). 기능 설정에서 실시간 보기가 켜져 있는지 확인하세요."
                        : "연결이 끊어졌습니다. 기능 설정에서 실시간 보기가 켜져 있는지 확인하세요.";
                Ui.main(() -> connectionLost(why));
            }

            @Override public void onClosed(WebSocket s, int code, String reason) {
                Ui.main(() -> connectionLost("연결이 끊어졌습니다. 기능 설정에서 실시간 보기가 켜져 있는지 확인하세요."));
            }
        });
    }

    private void onText(String text) {
        if (closed) return;
        JSONObject m;
        try { m = new JSONObject(text); } catch (Exception e) { return; }
        switch (m.optString("t")) {
            case "device":
                online = m.optBoolean("online");
                badge.setVisibility(online ? View.VISIBLE : View.GONE);
                if (online) show(img.getVisibility() == View.VISIBLE ? "" : "영상을 기다리는 중…");
                else { img.setVisibility(View.GONE); show("단말 연결을 기다리는 중…"); }
                break;
            case "hello":
                if (m.has("mic") && !m.optBoolean("mic", true)) {
                    listenBtn.setEnabled(false);
                    status.setText("이 단말은 마이크 입력이 설정되지 않았습니다.");
                }
                break;
            case "mic_denied":
                Ui.toast(this, "기능 설정에서 단말 소리 듣기가 꺼져 있습니다.");
                setListen(false);
                break;
            case "end":
                ended = true;
                show("연결 시간(10분)이 끝났습니다. 다시 열면 이어서 볼 수 있습니다.");
                break;
        }
    }

    private void onFrame(Bitmap bm) {
        if (closed) return;
        img.setImageBitmap(bm);
        if (img.getVisibility() != View.VISIBLE) { img.setVisibility(View.VISIBLE); show(""); }
        frames++;
        long now = System.currentTimeMillis();
        if (now - fpsAt > 2000) {
            status.setText(String.format(java.util.Locale.US, "%.1f fps", frames * 1000.0 / (now - fpsAt)));
            frames = 0;
            fpsAt = now;
        }
    }

    private void connectionLost(String why) {
        if (closed) return;
        badge.setVisibility(View.GONE);
        if (!ended) show(why);
        stopTalk();
    }

    private void sendJson(String s) { if (ws != null) ws.send(s); }

    // ---- 단말 마이크 → 폰 스피커 ----
    private void setListen(boolean on) {
        listening = on;
        listenBtn.setText(on ? "듣기 끄기" : "단말 소리 듣기");
        listenBtn.setBackground(Ui.box(on ? Ui.BAD : Ui.FG, 0, 8, this));
        if (on) startPlayer();
        else playQueue.clear();
        sendJson("{\"t\":\"mic\",\"on\":" + on + "}");
    }

    private void startPlayer() {
        if (track != null) return;
        int min = AudioTrack.getMinBufferSize(RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        track = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(Math.max(min, RATE)) // 약 0.5초
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build();
        track.play();
        AudioTrack t = track;
        playThread = new Thread(() -> {
            try {
                while (!closed) {
                    byte[] pcm = playQueue.take();
                    // 밀리면 지연이 쌓이지 않게 오래된 조각을 버린다
                    while (playQueue.size() > 6) playQueue.poll();
                    t.write(pcm, 0, pcm.length & ~1);
                }
            } catch (InterruptedException ignored) { }
        }, "live-play");
        playThread.start();
    }

    // ---- 폰 마이크 → 단말 스피커 ----
    private void toggleTalk() {
        if (talking) { stopTalk(); return; }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQ_MIC);
            return;
        }
        startTalk();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        if (code != REQ_MIC) return;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) startTalk();
        else Ui.toast(this, "말하려면 마이크 권한을 허용해 주세요.");
    }

    @SuppressWarnings("MissingPermission")
    private void startTalk() {
        int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord rec;
        try {
            rec = new AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION, RATE, AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT, Math.max(min, TALK_CHUNK * 4));
            if (rec.getState() != AudioRecord.STATE_INITIALIZED) throw new IllegalStateException();
        } catch (Exception e) {
            Ui.toast(this, "마이크를 열 수 없습니다.");
            return;
        }
        talking = true;
        talkBtn.setText("말하기 끝");
        talkBtn.setBackground(Ui.box(Ui.BAD, 0, 8, this));
        rec.startRecording();
        talkThread = new Thread(() -> {
            byte[] buf = new byte[TALK_CHUNK];
            try {
                while (talking && !closed) {
                    int off = 0;
                    while (off < TALK_CHUNK && talking) {
                        int n = rec.read(buf, off, TALK_CHUNK - off);
                        if (n <= 0) break;
                        off += n;
                    }
                    if (off < TALK_CHUNK) continue;
                    byte[] out = new byte[1 + TALK_CHUNK];
                    out[0] = 3;
                    System.arraycopy(buf, 0, out, 1, TALK_CHUNK);
                    WebSocket s = ws;
                    if (s != null && s.queueSize() < 64 * 1024) s.send(ByteString.of(out));
                }
            } finally {
                try { rec.stop(); } catch (Exception ignored) { }
                rec.release();
            }
        }, "live-talk");
        talkThread.start();
    }

    private void stopTalk() {
        talking = false;
        if (talkBtn != null) {
            talkBtn.setText("말하기");
            talkBtn.setBackground(Ui.box(Ui.FG, 0, 8, this));
        }
    }

    @Override
    protected void onStop() {
        super.onStop();
        // 화면을 벗어나면 연결을 끊는다 (탭을 열어 둔 채 잊어도 단말이 계속 스트리밍하지 않도록)
        if (!isChangingConfigurations()) finish();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        closed = true;
        Ui.cancel(noDevice);
        stopTalk();
        if (ws != null) ws.close(1000, "bye");
        if (playThread != null) playThread.interrupt();
        if (track != null) { try { track.stop(); } catch (Exception ignored) { } track.release(); }
    }
}
