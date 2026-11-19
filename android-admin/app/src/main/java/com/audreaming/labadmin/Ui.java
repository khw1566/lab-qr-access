package com.audreaming.labadmin;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** 화면 조각을 코드로 만드는 도우미. 웹 관리자의 잉크색·흰 바탕 스타일을 따른다. */
public final class Ui {
    public static final int FG = 0xFF111827, MUTED = 0xFF6B7280, LINE = 0xFFE5E7EB, LINE_STRONG = 0xFFD1D5DB;
    public static final int SUNKEN = 0xFFF5F6F8, BG = 0xFFF5F6F8, SURFACE = 0xFFFFFFFF;
    public static final int OK = 0xFF15803D, OK_SOFT = 0xFFECFDF3, WARN = 0xFFB45309, WARN_SOFT = 0xFFFFF7ED;
    public static final int BAD = 0xFFB91C1C, BAD_SOFT = 0xFFFEF2F2, MASTER = 0xFFF0A500, MASTER_INK = 0xFF1B1400, LINK = 0xFF1D4ED8;

    private static final ExecutorService POOL = Executors.newCachedThreadPool();
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    public interface Task<T> { T run() throws Exception; }
    public interface Done<T> { void done(T value); }

    /** 백그라운드에서 task를 돌리고 결과를 화면 스레드로 넘긴다. 실패하면 서버가 준 문구를 토스트로 보여 준다. */
    public static <T> void async(Activity a, Task<T> task, Done<T> ok) { async(a, task, ok, null); }

    public static <T> void async(Activity a, Task<T> task, Done<T> ok, Done<Exception> err) {
        POOL.execute(() -> {
            try {
                T v = task.run();
                MAIN.post(() -> { if (!a.isFinishing() && !a.isDestroyed()) ok.done(v); });
            } catch (Exception e) {
                MAIN.post(() -> {
                    if (a.isFinishing() || a.isDestroyed()) return;
                    if (e instanceof Api.ApiException && ((Api.ApiException) e).status == 401 && a instanceof MainActivity) {
                        ((MainActivity) a).sessionExpired();
                        return;
                    }
                    if (err != null) err.done(e);
                    else toast(a, e.getMessage() != null ? e.getMessage() : "오류가 발생했습니다.");
                });
            }
        });
    }

    public static void runBg(Runnable r) { POOL.execute(r); }
    public static void main(Runnable r) { MAIN.post(r); }
    public static void later(Runnable r, long ms) { MAIN.postDelayed(r, ms); }
    public static void cancel(Runnable r) { MAIN.removeCallbacks(r); }

    public static void toast(Context c, String msg) { Toast.makeText(c, msg, Toast.LENGTH_LONG).show(); }

    public static int dp(Context c, float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, c.getResources().getDisplayMetrics()));
    }

    public static GradientDrawable box(int fill, int stroke, float radiusDp, Context c) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(c, radiusDp));
        if (stroke != 0) g.setStroke(dp(c, 1), stroke);
        return g;
    }

    public static LinearLayout vbox(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.VERTICAL);
        return l;
    }

    public static LinearLayout hbox(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setGravity(Gravity.CENTER_VERTICAL);
        return l;
    }

    /** 줄바꿈되는 버튼 묶음 (간단한 흐름 배치). */
    public static FlowLayout flow(Context c) { return new FlowLayout(c, dp(c, 8), dp(c, 8)); }

    public static TextView text(Context c, CharSequence s, float sp, int color) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setLineSpacing(0, 1.15f);
        return t;
    }

    public static TextView bold(Context c, CharSequence s, float sp) {
        TextView t = text(c, s, sp, FG);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    public static TextView small(Context c, CharSequence s) { return text(c, s, 13, MUTED); }

    public static TextView mono(Context c, CharSequence s, float sp, int color) {
        TextView t = text(c, s, sp, color);
        t.setTypeface(Typeface.MONOSPACE);
        return t;
    }

    public static TextView h2(Context c, String s) {
        TextView t = bold(c, s, 20);
        t.setPadding(0, 0, 0, dp(c, 4));
        return t;
    }

    public static TextView groupTitle(Context c, String s) {
        TextView t = bold(c, s, 15);
        t.setPadding(dp(c, 2), dp(c, 20), 0, dp(c, 8));
        return t;
    }

    /** 흰 카드 (웹의 .panel). */
    public static LinearLayout card(Context c) {
        LinearLayout l = vbox(c);
        l.setBackground(box(SURFACE, LINE, 10, c));
        int p = dp(c, 14);
        l.setPadding(p, p, p, p);
        return l;
    }

    public static LinearLayout.LayoutParams lp(Context c, int topDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        p.topMargin = dp(c, topDp);
        return p;
    }

    public static void add(LinearLayout parent, View v, int topDp) { parent.addView(v, lp(parent.getContext(), topDp)); }

    public static final int PRIMARY = 0, SECONDARY = 1, GOOD = 2, DANGER = 3, QUIET = 4;

    public static Button button(Context c, String label, int kind) {
        Button b = new Button(c);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(14);
        b.setStateListAnimator(null);
        b.setMinHeight(dp(c, 38));
        b.setMinimumHeight(dp(c, 38));
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setPadding(dp(c, 14), 0, dp(c, 14), 0);
        int fill, fg, stroke;
        switch (kind) {
            case GOOD: fill = OK; fg = Color.WHITE; stroke = 0; break;
            case DANGER: fill = BAD; fg = Color.WHITE; stroke = 0; break;
            case SECONDARY: fill = SURFACE; fg = FG; stroke = LINE_STRONG; break;
            case QUIET: fill = 0x00000000; fg = LINK; stroke = 0; break;
            default: fill = FG; fg = Color.WHITE; stroke = 0;
        }
        b.setTextColor(new ColorStateList(new int[][]{{-android.R.attr.state_enabled}, {}}, new int[]{0xFF9CA3AF, fg}));
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x22000000), box(fill, stroke, 8, c), null));
        return b;
    }

    public static Button smallButton(Context c, String label, int kind, View.OnClickListener l) {
        Button b = button(c, label, kind);
        b.setTextSize(13);
        b.setMinHeight(dp(c, 34));
        b.setMinimumHeight(dp(c, 34));
        b.setPadding(dp(c, 12), 0, dp(c, 12), 0);
        b.setOnClickListener(l);
        return b;
    }

    /** 상태 배지 (웹의 .pill). */
    public static TextView pill(Context c, String s, int fg, int bg) {
        TextView t = text(c, s, 12, fg);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setBackground(box(bg, 0, 999, c));
        t.setPadding(dp(c, 8), dp(c, 2), dp(c, 8), dp(c, 2));
        return t;
    }

    public static View wrapLeft(View v) {
        LinearLayout l = hbox(v.getContext());
        l.addView(v);
        return l;
    }

    public static View banner(Context c, String s, boolean alert) {
        TextView t = text(c, s, 14, alert ? BAD : FG);
        t.setBackground(box(alert ? BAD_SOFT : SUNKEN, alert ? 0xFFFECACA : LINE, 10, c));
        int p = dp(c, 12);
        t.setPadding(p, p, p, p);
        return t;
    }

    public static View empty(Context c, String s) {
        TextView t = text(c, s, 14, MUTED);
        t.setGravity(Gravity.CENTER);
        int p = dp(c, 28);
        t.setPadding(p, p, p, p);
        LinearLayout card = card(c);
        card.addView(t);
        return card;
    }

    public static EditText input(Context c, String value, String hint) {
        EditText e = new EditText(c);
        e.setText(value);
        e.setHint(hint);
        e.setTextSize(15);
        e.setTextColor(FG);
        e.setSingleLine(true);
        e.setBackground(box(SURFACE, LINE_STRONG, 8, c));
        int p = dp(c, 10);
        e.setPadding(p, p, p, p);
        return e;
    }

    public static EditText multiline(Context c, String value, int minLines) {
        EditText e = input(c, value, "");
        e.setSingleLine(false);
        e.setMinLines(minLines);
        e.setGravity(Gravity.TOP | Gravity.START);
        e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        return e;
    }

    public static EditText number(Context c, int value) {
        EditText e = input(c, String.valueOf(value), "");
        e.setInputType(InputType.TYPE_CLASS_NUMBER);
        e.setTypeface(Typeface.MONOSPACE);
        return e;
    }

    /** 이름표 + 입력칸 묶음. */
    public static LinearLayout field(Context c, String label, View input) {
        LinearLayout l = vbox(c);
        TextView t = text(c, label, 13, MUTED);
        t.setPadding(dp(c, 2), 0, 0, dp(c, 4));
        l.addView(t);
        l.addView(input, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return l;
    }

    public static Spinner spinner(Context c, List<String> labels, int selected) {
        Spinner s = new Spinner(c, Spinner.MODE_DROPDOWN);
        ArrayAdapter<String> a = new ArrayAdapter<>(c, android.R.layout.simple_spinner_item, labels);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        s.setAdapter(a);
        if (selected >= 0 && selected < labels.size()) s.setSelection(selected);
        s.setBackground(box(SURFACE, LINE_STRONG, 8, c));
        s.setPadding(dp(c, 4), dp(c, 6), dp(c, 4), dp(c, 6));
        return s;
    }

    /** 설정 한 줄: 제목·설명 + 오른쪽 컨트롤. */
    public static LinearLayout settingRow(Context c, String title, String desc, View control, boolean danger) {
        LinearLayout row = hbox(c);
        row.setPadding(0, dp(c, 10), 0, dp(c, 10));
        LinearLayout txt = vbox(c);
        TextView t = bold(c, title, 15);
        if (danger) t.setTextColor(BAD);
        txt.addView(t);
        if (desc != null && !desc.isEmpty()) txt.addView(small(c, desc));
        row.addView(txt, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        p.leftMargin = dp(c, 12);
        row.addView(control, p);
        return row;
    }

    public static View divider(Context c) {
        View v = new View(c);
        v.setBackgroundColor(LINE);
        v.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 1));
        return v;
    }

    public static Switch toggle(Context c, boolean on) {
        Switch s = new Switch(c);
        s.setChecked(on);
        return s;
    }

    public static CheckBox check(Context c, String label, boolean on) {
        CheckBox b = new CheckBox(c);
        b.setText(label);
        b.setChecked(on);
        b.setTextColor(FG);
        return b;
    }

    // ---------- 대화상자 ----------

    public interface Confirmed { void ok(); }
    public interface Entered { void ok(String text); }

    public static void confirm(Context c, String title, String text, String okLabel, boolean danger, Confirmed ok) {
        AlertDialog d = new AlertDialog.Builder(c)
                .setTitle(title)
                .setMessage(text)
                .setNegativeButton("취소", null)
                .setPositiveButton(okLabel, (x, y) -> ok.ok())
                .create();
        d.show();
        if (danger) d.getButton(AlertDialog.BUTTON_POSITIVE).setTextColor(BAD);
    }

    /** 관리자 멘트처럼 글을 받는 대화상자. 취소하면 아무것도 하지 않는다. */
    public static void prompt(Context c, String title, String text, String label, String value, boolean multiline,
                              String okLabel, boolean danger, Entered ok) {
        LinearLayout l = vbox(c);
        int p = dp(c, 20);
        l.setPadding(p, dp(c, 8), p, 0);
        if (text != null && !text.isEmpty()) Ui.add(l, text(c, text, 14, FG), 0);
        EditText e = multiline ? multiline(c, value, 3) : input(c, value, "");
        Ui.add(l, field(c, label, e), 12);
        AlertDialog d = new AlertDialog.Builder(c)
                .setTitle(title)
                .setView(l)
                .setNegativeButton("취소", null)
                .setPositiveButton(okLabel, (x, y) -> ok.ok(e.getText().toString().trim()))
                .create();
        d.show();
        if (danger) d.getButton(AlertDialog.BUTTON_POSITIVE).setTextColor(BAD);
    }

    public static ScrollView scroll(View content) {
        ScrollView s = new ScrollView(content.getContext());
        s.setFillViewport(true);
        s.addView(content);
        return s;
    }
}
