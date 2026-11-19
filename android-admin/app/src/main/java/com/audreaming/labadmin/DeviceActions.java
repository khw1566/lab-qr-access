package com.audreaming.labadmin;

import android.content.Intent;
import android.widget.Button;

import org.json.JSONObject;

/** 현황·출입 단말 화면이 함께 쓰는 원격 열기와 실시간 보기. */
public final class DeviceActions {

    /** 명령을 넣고 보드가 가져갔는지 1초마다 확인한다 (20초 안에 안 가져가면 서버가 취소). */
    public static void remoteOpen(MainActivity a, int id, String name, Button b) {
        Ui.confirm(a, name + " 문을 열까요?",
                "단말의 릴레이가 작동해 출입문이 열리고, 단말 화면에 관리자 원격 열림 안내가 나옵니다. 출입 기록에 남습니다.",
                "문 열기", false, () -> {
                    Api api = Api.get(a);
                    CharSequence label = b.getText();
                    b.setEnabled(false);
                    b.setText("전달 중…");
                    Ui.async(a, () -> {
                        int cid = api.post("/api/admin/devices/" + id + "/open", null).getInt("id");
                        for (int i = 0; i < 40; i++) {
                            Thread.sleep(1000);
                            JSONObject c = api.get("/api/admin/door-commands/" + cid);
                            String st = c.optString("state");
                            if (!"pending".equals(st)) return st;
                        }
                        return "expired";
                    }, st -> {
                        b.setEnabled(true);
                        b.setText(label);
                        if ("delivered".equals(st)) Ui.toast(a, name + " 문을 열었습니다.");
                        else Ui.toast(a, name + " 단말이 응답하지 않아 취소했습니다. 단말 연결 상태를 확인하세요.");
                        a.dataChanged();
                    }, e -> {
                        b.setEnabled(true);
                        b.setText(label);
                        Ui.toast(a, e.getMessage());
                    });
                });
    }

    public static void liveView(MainActivity a, int id, String name) {
        if (!a.adminSettings.optBoolean("live_view", true)) {
            Ui.toast(a, "기능 설정에서 실시간 보기가 꺼져 있습니다.");
            return;
        }
        Intent i = new Intent(a, LiveActivity.class);
        i.putExtra("id", id);
        i.putExtra("name", name);
        i.putExtra("mic", a.adminSettings.optBoolean("live_mic", false));
        a.startActivity(i);
    }
}
