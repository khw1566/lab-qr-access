package com.audreaming.labadmin;

import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

/** 관리자 화면 한 장 (웹의 A.overview, A.requests …). load()에서 데이터를 받아 a.show()로 그린다. */
public abstract class Screen {
    protected final MainActivity a;
    protected final Api api;

    protected Screen(MainActivity a) {
        this.a = a;
        this.api = Api.get(a);
    }

    public abstract String title();

    /** 다른 관리자가 데이터를 바꿨을 때 조용히 다시 불러와도 되는 화면인지 (입력 중인 글이 있는 화면은 false). */
    public boolean autoRefresh() { return true; }

    public abstract void load();

    protected void reload() { a.reloadCurrent(); }

    /** 화면 맨 위 제목과 설명. */
    protected LinearLayout page(String title, String desc) {
        LinearLayout root = Ui.vbox(a);
        int p = Ui.dp(a, 16);
        root.setPadding(p, p, p, Ui.dp(a, 40));
        root.addView(Ui.h2(a, title));
        if (desc != null && !desc.isEmpty()) root.addView(Ui.small(a, desc));
        return root;
    }

    /** 승인 대기·승인됨처럼 목록을 고르는 단추 묶음. */
    protected View segment(String[][] opts, String current, Ui.Entered pick) {
        FlowLayout f = Ui.flow(a);
        for (String[] o : opts) {
            boolean on = o[0].equals(current);
            f.addView(Ui.smallButton(a, o[1], on ? Ui.PRIMARY : Ui.SECONDARY, v -> pick.ok(o[0])));
        }
        return f;
    }

    /** 이름 + 회색 보조 글 줄. */
    protected LinearLayout nameLine(String name, String sub) {
        LinearLayout l = Ui.hbox(a);
        TextView n = Ui.bold(a, name, 16);
        l.addView(n);
        if (sub != null && !sub.isEmpty()) {
            TextView s = Ui.mono(a, "  " + sub, 13, Ui.MUTED);
            l.addView(s);
        }
        return l;
    }

    /** 처리 요청 공통: 성공하면 토스트와 함께 다시 불러온다. */
    protected void act(Ui.Task<JSONObject> task, String okMsg) {
        Ui.async(a, task, r -> {
            if (okMsg != null) Ui.toast(a, okMsg);
            a.dataChanged();
            reload();
        });
    }

    protected static JSONObject reasonBody(String reason) {
        try { return new JSONObject().put("reason", reason); } catch (Exception e) { return new JSONObject(); }
    }
}
