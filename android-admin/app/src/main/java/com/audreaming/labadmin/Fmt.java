package com.audreaming.labadmin;

import org.json.JSONObject;

import java.util.Calendar;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.TimeZone;

/** 날짜·시각은 웹 화면과 같이 한국 시간으로 보여 준다. */
public final class Fmt {
    private static final TimeZone KST = TimeZone.getTimeZone("Asia/Seoul");
    private static final String[] WD = {"일", "월", "화", "수", "목", "금", "토"};

    public static final Map<String, String> REASON = new HashMap<>();
    public static final Map<String, String> PT = new HashMap<>();
    static {
        String[] r = {
            "invalid_token", "잘못된 QR", "expired_token", "만료된 QR", "token_reused", "이미 사용한 QR", "no_reservation", "없는 신청",
            "not_approved", "승인 안 됨", "user_blocked", "정지된 사용자", "wrong_room", "다른 실습실", "out_of_time", "이용 시간 아님",
            "lockdown", "긴급 잠금 중", "room_closed", "실습실 운영 중지", "master", "마스터 QR", "master_disabled", "마스터 QR 꺼짐",
            "special", "특별 출입", "special_expired", "특별 출입 만료", "special_revoked", "특별 출입 승인 취소",
            "master_revoked", "폐기된 마스터 QR", "master_expired", "기간 끝난 마스터 QR", "duplicate_scan", "중복 인식",
            "remote", "관리자 원격 열기", "manual_invalid", "잘못된 입력 코드", "manual_rate_limited", "코드 입력 횟수 초과",
        };
        for (int i = 0; i < r.length; i += 2) REASON.put(r[i], r[i + 1]);
        PT.put("replicator", "Replicator+");
        PT.put("z18", "Z18");
    }

    public static Calendar cal(long ms) {
        Calendar c = Calendar.getInstance(KST, Locale.KOREA);
        c.setTimeInMillis(ms);
        return c;
    }

    private static String pad(int n) { return n < 10 ? "0" + n : String.valueOf(n); }

    public static String date(long ms) {
        Calendar c = cal(ms);
        return (c.get(Calendar.MONTH) + 1) + "/" + c.get(Calendar.DAY_OF_MONTH) + "(" + WD[c.get(Calendar.DAY_OF_WEEK) - 1] + ")";
    }

    public static String time(long ms) {
        Calendar c = cal(ms);
        return pad(c.get(Calendar.HOUR_OF_DAY)) + ":" + pad(c.get(Calendar.MINUTE));
    }

    public static String dt(long ms) { return date(ms) + " " + time(ms); }

    public static String range(long a, long b) { return date(a) + " " + time(a) + "–" + time(b); }

    public static String isoDay(long ms) {
        Calendar c = cal(ms);
        return c.get(Calendar.YEAR) + "-" + pad(c.get(Calendar.MONTH) + 1) + "-" + pad(c.get(Calendar.DAY_OF_MONTH));
    }

    public static String full(long ms) {
        Calendar c = cal(ms);
        return isoDay(ms) + " " + time(ms) + ":" + pad(c.get(Calendar.SECOND));
    }

    public static String ago(long ms) {
        long s = (System.currentTimeMillis() - ms) / 1000;
        if (s < 60) return "방금";
        if (s < 3600) return (s / 60) + "분 전";
        if (s < 86400) return (s / 3600) + "시간 전";
        return dt(ms);
    }

    /** JSON 숫자 칸 (null이면 0). */
    public static long ms(JSONObject o, String k) { return o.isNull(k) ? 0 : o.optLong(k, 0); }

    public static String str(JSONObject o, String k) { return o.isNull(k) ? "" : o.optString(k, ""); }
}
