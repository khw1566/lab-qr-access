package com.audreaming.labadmin;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

/** 관리자 로그인. 웹과 같은 학번·비밀번호를 쓰고, 관리자 권한이 있는 계정만 들어갈 수 있다. */
public class LoginActivity extends Activity {
    private EditText sno, pw, server;
    private Button go;
    private TextView err;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        Api api = Api.get(this);
        if (api.hasSession()) { openMain(); return; }
        buildUi(api);
    }

    private void buildUi(Api api) {
        LinearLayout root = Ui.vbox(this);
        root.setBackgroundColor(Ui.BG);
        int p = Ui.dp(this, 24);
        root.setPadding(p, Ui.dp(this, 56), p, p);
        root.setGravity(Gravity.CENTER_HORIZONTAL);

        ImageView logo = new ImageView(this);
        logo.setImageResource(R.mipmap.ic_launcher);
        root.addView(logo, new LinearLayout.LayoutParams(Ui.dp(this, 72), Ui.dp(this, 72)));
        TextView title = Ui.bold(this, "실습실 출입 관리자", 22);
        title.setGravity(Gravity.CENTER);
        Ui.add(root, title, 14);
        TextView sub = Ui.small(this, "웹 관리자와 같은 학번·비밀번호로 로그인하세요.");
        sub.setGravity(Gravity.CENTER);
        Ui.add(root, sub, 4);

        LinearLayout card = Ui.card(this);
        sno = Ui.input(this, getSharedPreferences("api", MODE_PRIVATE).getString("last_sno", ""), "학번");
        pw = Ui.input(this, "", "비밀번호");
        pw.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        pw.setImeOptions(EditorInfo.IME_ACTION_GO);
        pw.setOnEditorActionListener((v, id, e) -> { submit(); return true; });
        server = Ui.input(this, api.base(), Api.DEFAULT_BASE);
        server.setInputType(InputType.TYPE_TEXT_VARIATION_URI);
        server.setTextSize(13);
        err = Ui.text(this, "", 14, Ui.BAD);
        err.setVisibility(TextView.GONE);
        go = Ui.button(this, "로그인", Ui.PRIMARY);
        go.setOnClickListener(v -> submit());
        Ui.add(card, Ui.field(this, "학번", sno), 0);
        Ui.add(card, Ui.field(this, "비밀번호", pw), 12);
        Ui.add(card, Ui.field(this, "서버 주소", server), 12);
        Ui.add(card, err, 10);
        Ui.add(card, go, 14);
        Ui.add(root, card, 24);

        setContentView(Ui.scroll(root));
    }

    private void submit() {
        String s = sno.getText().toString().trim(), password = pw.getText().toString();
        if (s.isEmpty() || password.isEmpty()) { showErr("학번과 비밀번호를 입력해 주세요."); return; }
        Api api = Api.get(this);
        api.setBase(server.getText().toString().isEmpty() ? Api.DEFAULT_BASE : server.getText().toString());
        go.setEnabled(false);
        go.setText("확인 중…");
        err.setVisibility(TextView.GONE);
        Ui.async(this, () -> {
            JSONObject body = new JSONObject().put("student_no", s).put("password", password);
            api.post("/api/auth/login", body);
            try {
                api.get("/api/admin/check");
            } catch (Api.ApiException e) {
                if (e.status == 403) {
                    api.post("/api/auth/logout", null);
                    api.clearSession();
                    throw new Api.ApiException(403, "관리자 권한이 없는 계정입니다. 관리자 계정으로 로그인하세요.");
                }
                throw e;
            }
            return true;
        }, ok -> {
            getSharedPreferences("api", MODE_PRIVATE).edit().putString("last_sno", s).apply();
            openMain();
        }, e -> {
            go.setEnabled(true);
            go.setText("로그인");
            showErr(e.getMessage());
        });
    }

    private void showErr(String m) {
        err.setText(m);
        err.setVisibility(TextView.VISIBLE);
    }

    private void openMain() {
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }
}
