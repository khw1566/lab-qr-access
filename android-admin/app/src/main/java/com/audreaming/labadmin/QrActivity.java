package com.audreaming.labadmin;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.pdf.PdfDocument;
import android.net.Uri;
import android.os.Bundle;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.print.PageRange;
import android.print.PrintAttributes;
import android.print.PrintDocumentAdapter;
import android.print.PrintDocumentInfo;
import android.print.PrintManager;
import android.provider.MediaStore;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.google.zxing.BarcodeFormat;
import com.google.zxing.EncodeHintType;
import com.google.zxing.common.BitMatrix;
import com.google.zxing.qrcode.QRCodeWriter;
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel;

import org.json.JSONObject;

import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.EnumMap;
import java.util.Map;

/** 마스터 QR 보기. 화면용은 웹처럼 5초마다 같은 t로 다시 물어 쓰였거나 만료가 가까우면 새 QR로 바꾸고, 인쇄용은 저장·공유·인쇄한다. */
public class QrActivity extends Activity {
    private int id;
    private boolean isStatic;
    private String label, sub;
    private ImageView qr;
    private TextView count, code, codeLabel, err;
    private JSONObject cur;
    private long until, total = 300_000;
    private boolean alive;
    private Bitmap staticBitmap;

    private final Runnable tick = this::fetch;
    private final Runnable countdown = new Runnable() {
        @Override public void run() {
            if (!alive) return;
            updateCount();
            Ui.later(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        id = getIntent().getIntExtra("id", 0);
        isStatic = getIntent().getBooleanExtra("static", false);
        label = getIntent().getStringExtra("label");
        sub = getIntent().getStringExtra("sub");
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.screenBrightness = 1f; // 카메라가 잘 읽도록 화면을 밝게
        getWindow().setAttributes(lp);
        // 마스터 QR 화면은 캡처·화면 녹화를 막는다 (인쇄용은 저장 기능이 따로 있음)
        if (!isStatic) getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        buildUi();
    }

    private void buildUi() {
        LinearLayout root = Ui.vbox(this);
        root.setBackgroundColor(isStatic ? Ui.BG : Ui.MASTER);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        int p = Ui.dp(this, 20);
        root.setPadding(p, p, p, p);

        LinearLayout head = Ui.hbox(this);
        LinearLayout t = Ui.vbox(this);
        TextView title = Ui.bold(this, (isStatic ? "인쇄용 마스터 QR · " : "마스터 · ") + label, 20);
        title.setTextColor(isStatic ? Ui.FG : Ui.MASTER_INK);
        t.addView(title);
        TextView s = Ui.text(this, sub, 14, isStatic ? Ui.MUTED : Ui.MASTER_INK);
        t.addView(s);
        head.addView(t, new LinearLayout.LayoutParams(0, -2, 1));
        head.addView(Ui.smallButton(this, "닫기", Ui.SECONDARY, v -> finish()));
        root.addView(head, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout card = Ui.card(this);
        card.setGravity(Gravity.CENTER_HORIZONTAL);
        qr = new ImageView(this);
        qr.setBackgroundColor(Color.WHITE);
        int size = Math.min(getResources().getDisplayMetrics().widthPixels - Ui.dp(this, 88), Ui.dp(this, 320));
        card.addView(qr, new LinearLayout.LayoutParams(size, size));
        err = Ui.text(this, "", 14, Ui.BAD);
        err.setGravity(Gravity.CENTER);
        err.setVisibility(View.GONE);
        card.addView(err);
        count = Ui.mono(this, "", 16, Ui.FG);
        count.setGravity(Gravity.CENTER);
        Ui.add(card, count, 10);
        codeLabel = Ui.small(this, "카메라가 읽지 못하면 단말 코드 입력 화면에 아래 번호를 넣으세요");
        codeLabel.setGravity(Gravity.CENTER);
        codeLabel.setVisibility(View.GONE);
        Ui.add(card, codeLabel, 10);
        code = Ui.mono(this, "", 30, Ui.FG);
        code.setGravity(Gravity.CENTER);
        code.setLetterSpacing(0.1f);
        code.setVisibility(View.GONE);
        card.addView(code);
        LinearLayout.LayoutParams cp = new LinearLayout.LayoutParams(-1, -2);
        cp.topMargin = Ui.dp(this, 18);
        root.addView(card, cp);

        TextView hint = Ui.text(this, isStatic
                ? "인쇄용 고정 QR은 사진을 찍으면 누구나 쓸 수 있습니다. 비상용으로 금고 등에 보관하고, 분실하면 바로 재발급하거나 폐기하세요."
                : "마스터 QR입니다. 다른 사람에게 보여 주거나 캡처해서 보내지 마세요. 한 번 쓰면 새 QR로 바뀝니다.", 14, isStatic ? Ui.MUTED : Ui.MASTER_INK);
        hint.setGravity(Gravity.CENTER);
        Ui.add(root, hint, 14);

        if (isStatic) {
            LinearLayout row = Ui.hbox(this);
            row.setGravity(Gravity.CENTER);
            row.addView(Ui.smallButton(this, "이미지 저장", Ui.SECONDARY, v -> save()));
            row.addView(space());
            row.addView(Ui.smallButton(this, "공유", Ui.SECONDARY, v -> share()));
            row.addView(space());
            row.addView(Ui.smallButton(this, "인쇄", Ui.PRIMARY, v -> print()));
            Ui.add(root, row, 14);
        }

        root.setOnApplyWindowInsetsListener((v, insets) -> {
            android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars());
            v.setPadding(p + i.left, p + i.top, p + i.right, p + i.bottom);
            return WindowInsets.CONSUMED;
        });
        setContentView(Ui.scroll(root));
    }

    private View space() {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(Ui.dp(this, 8), 1));
        return v;
    }

    @Override
    protected void onResume() {
        super.onResume();
        alive = true;
        cur = null;
        fetch();
    }

    @Override
    protected void onPause() {
        super.onPause();
        // 웹처럼 화면을 벗어나면 갱신을 멈춘다
        alive = false;
        Ui.cancel(tick);
        Ui.cancel(countdown);
    }

    private void fetch() {
        Ui.cancel(tick);
        if (!alive) return;
        Api api = Api.get(this);
        String keep = cur != null && !isStatic ? "?keep=" + cur.optLong("t") : "";
        Ui.async(this, () -> api.get("/api/admin/masters/" + id + "/qr" + keep), r -> {
            if (!alive) return;
            err.setVisibility(View.GONE);
            if (cur == null || !r.optString("token").equals(cur.optString("token"))) {
                Bitmap bm = qrBitmap(r.optString("token"), 600);
                qr.setImageBitmap(bm);
                if (isStatic) staticBitmap = bm;
                if (r.optBoolean("used_prev")) Ui.toast(this, "출입 확인됨 · 새 마스터 QR로 바뀌었습니다.");
                total = r.optLong("valid_ms", 300_000);
                until = System.currentTimeMillis() + (r.optLong("expires_at") - r.optLong("server_time", System.currentTimeMillis()));
            }
            cur = r;
            if (isStatic) { count.setText(Fmt.dt(r.optLong("expires_at")) + "까지 유효"); return; }
            String mc = Fmt.str(r, "manual_code");
            boolean manual = mc.matches("\\d{8}") && until > System.currentTimeMillis();
            code.setText(manual ? mc.substring(0, 4) + " " + mc.substring(4) : "");
            code.setVisibility(manual ? View.VISIBLE : View.GONE);
            codeLabel.setVisibility(manual ? View.VISIBLE : View.GONE);
            Ui.cancel(countdown);
            countdown.run();
            Ui.later(tick, r.optLong("poll_ms", 5000));
        }, e -> {
            if (!alive) return;
            int st = e instanceof Api.ApiException ? ((Api.ApiException) e).status : 0;
            // 잠깐의 통신 끊김이면 지금 QR을 그대로 두고 다시 물어봄
            if (cur != null && (st == 0 || st == 429 || st >= 500) && until > System.currentTimeMillis()) {
                Ui.later(tick, 3000);
                return;
            }
            cur = null;
            Ui.cancel(countdown);
            qr.setImageBitmap(null);
            count.setText("");
            code.setVisibility(View.GONE);
            codeLabel.setVisibility(View.GONE);
            err.setText(e.getMessage());
            err.setVisibility(View.VISIBLE);
        });
    }

    private void updateCount() {
        long left = Math.max(0, until - System.currentTimeMillis());
        long sec = (left + 999) / 1000;
        count.setText(String.format(java.util.Locale.US, "%d:%02d 동안 유효", sec / 60, sec % 60));
        if (left == 0) { code.setVisibility(View.GONE); codeLabel.setVisibility(View.GONE); }
    }

    static Bitmap qrBitmap(String text, int px) {
        try {
            Map<EncodeHintType, Object> hints = new EnumMap<>(EncodeHintType.class);
            hints.put(EncodeHintType.ERROR_CORRECTION, ErrorCorrectionLevel.M);
            hints.put(EncodeHintType.MARGIN, 4);
            BitMatrix m = new QRCodeWriter().encode(text, BarcodeFormat.QR_CODE, 0, 0, hints);
            int n = m.getWidth(), cell = Math.max(1, px / n), size = n * cell;
            Bitmap bm = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(bm);
            c.drawColor(Color.WHITE);
            Paint p = new Paint();
            p.setColor(Color.BLACK);
            for (int y = 0; y < n; y++) for (int x = 0; x < n; x++)
                if (m.get(x, y)) c.drawRect(x * cell, y * cell, (x + 1) * cell, (y + 1) * cell, p);
            return bm;
        } catch (Exception e) {
            return null;
        }
    }

    // ---------- 인쇄용 QR: 저장·공유·인쇄 ----------

    private Uri saveToGallery() throws Exception {
        if (staticBitmap == null) throw new Exception("QR을 아직 불러오지 못했습니다.");
        ContentValues v = new ContentValues();
        v.put(MediaStore.Images.Media.DISPLAY_NAME, "master-qr-" + id + "-" + System.currentTimeMillis() + ".png");
        v.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
        v.put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/LabAdmin");
        Uri uri = getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, v);
        if (uri == null) throw new Exception("이미지를 저장하지 못했습니다.");
        try (OutputStream os = getContentResolver().openOutputStream(uri)) {
            staticBitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
        }
        return uri;
    }

    private void save() {
        try { saveToGallery(); Ui.toast(this, "사진 앱의 Pictures/LabAdmin 폴더에 저장했습니다."); }
        catch (Exception e) { Ui.toast(this, e.getMessage()); }
    }

    private void share() {
        try {
            Uri uri = saveToGallery();
            Intent i = new Intent(Intent.ACTION_SEND).setType("image/png").putExtra(Intent.EXTRA_STREAM, uri)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(i, "마스터 QR 공유"));
        } catch (Exception e) { Ui.toast(this, e.getMessage()); }
    }

    private void print() {
        if (staticBitmap == null) { Ui.toast(this, "QR을 아직 불러오지 못했습니다."); return; }
        PrintManager pm = (PrintManager) getSystemService(Context.PRINT_SERVICE);
        Bitmap bm = staticBitmap;
        String title = label, info = sub;
        pm.print("master-qr-" + id, new PrintDocumentAdapter() {
            @Override
            public void onLayout(PrintAttributes o, PrintAttributes n, CancellationSignal cs, LayoutResultCallback cb, Bundle extras) {
                cb.onLayoutFinished(new PrintDocumentInfo.Builder("master-qr-" + id + ".pdf")
                        .setContentType(PrintDocumentInfo.CONTENT_TYPE_DOCUMENT).setPageCount(1).build(), true);
            }

            @Override
            public void onWrite(PageRange[] pages, ParcelFileDescriptor dest, CancellationSignal cs, WriteResultCallback cb) {
                PdfDocument doc = new PdfDocument();
                PdfDocument.Page page = doc.startPage(new PdfDocument.PageInfo.Builder(595, 842, 1).create()); // A4 (pt)
                Canvas c = page.getCanvas();
                int side = 300, left = (595 - side) / 2, top = 140;
                c.drawBitmap(bm, null, new Rect(left, top, left + side, top + side), null);
                Paint tp = new Paint(Paint.ANTI_ALIAS_FLAG);
                tp.setTextAlign(Paint.Align.CENTER);
                tp.setTextSize(22);
                tp.setFakeBoldText(true);
                c.drawText(title, 595 / 2f, top + side + 40, tp);
                tp.setFakeBoldText(false);
                tp.setTextSize(13);
                c.drawText(info, 595 / 2f, top + side + 64, tp);
                tp.setTextSize(10);
                tp.setColor(0xFF555555);
                c.drawText("실습실 마스터 출입 QR · 분실 시 관리자에게 알려 주세요", 595 / 2f, top + side + 88, tp);
                doc.finishPage(page);
                try (FileOutputStream out = new FileOutputStream(dest.getFileDescriptor())) {
                    doc.writeTo(out);
                    cb.onWriteFinished(new PageRange[]{PageRange.ALL_PAGES});
                } catch (Exception e) {
                    cb.onWriteFailed(e.getMessage());
                } finally {
                    doc.close();
                }
            }
        }, null);
    }
}
