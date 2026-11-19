package com.audreaming.labadmin;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

import okhttp3.Cookie;
import okhttp3.CookieJar;
import okhttp3.HttpUrl;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;

/** 서버(/api/*) 호출. 웹 관리자와 같은 세션 쿠키(sid)를 쓰고, 앱을 다시 열어도 로그인이 유지되도록 저장해 둔다. */
public final class Api {
    public static final String DEFAULT_BASE = "https://lab.audreaming.com";
    private static final MediaType JSON = MediaType.get("application/json; charset=utf-8");
    private static Api instance;

    private final SharedPreferences prefs;
    private final OkHttpClient client;
    private final OkHttpClient wsClient;
    private String base;

    public static class ApiException extends Exception {
        public final int status;
        public ApiException(int status, String message) { super(message); this.status = status; }
    }

    public static synchronized Api get(Context ctx) {
        if (instance == null) instance = new Api(ctx.getApplicationContext());
        return instance;
    }

    private Api(Context ctx) {
        prefs = ctx.getSharedPreferences("api", Context.MODE_PRIVATE);
        base = prefs.getString("base", DEFAULT_BASE);
        CookieJar jar = new CookieJar() {
            @Override public void saveFromResponse(HttpUrl url, List<Cookie> cookies) {
                for (Cookie c : cookies) {
                    if (!"sid".equals(c.name())) continue;
                    // 로그아웃·비밀번호 변경 때 서버가 Max-Age=0 으로 지운다
                    if (c.value().isEmpty() || c.expiresAt() < System.currentTimeMillis()) prefs.edit().remove("sid").apply();
                    else prefs.edit().putString("sid", c.value()).apply();
                }
            }
            @Override public List<Cookie> loadForRequest(HttpUrl url) {
                List<Cookie> out = new ArrayList<>();
                String sid = prefs.getString("sid", null);
                if (sid != null) out.add(new Cookie.Builder().name("sid").value(sid).hostOnlyDomain(url.host()).path("/").secure().httpOnly().build());
                return out;
            }
        };
        client = new OkHttpClient.Builder()
                .cookieJar(jar)
                .connectTimeout(15, TimeUnit.SECONDS)
                .readTimeout(30, TimeUnit.SECONDS)
                .build();
        wsClient = client.newBuilder().readTimeout(0, TimeUnit.MILLISECONDS).pingInterval(20, TimeUnit.SECONDS).build();
    }

    public String base() { return base; }

    public void setBase(String b) {
        b = b.trim();
        while (b.endsWith("/")) b = b.substring(0, b.length() - 1);
        if (!b.startsWith("http://") && !b.startsWith("https://")) b = "https://" + b;
        if (!b.equals(base)) prefs.edit().remove("sid").apply();
        base = b;
        prefs.edit().putString("base", b).apply();
    }

    public boolean hasSession() { return prefs.getString("sid", null) != null; }

    public void clearSession() { prefs.edit().remove("sid").apply(); }

    public JSONObject get(String path) throws ApiException { return send("GET", path, null); }

    public JSONObject post(String path, JSONObject body) throws ApiException { return send("POST", path, body == null ? new JSONObject() : body); }

    public JSONObject patch(String path, JSONObject body) throws ApiException { return send("PATCH", path, body); }

    private JSONObject send(String method, String path, JSONObject body) throws ApiException {
        Request.Builder rb = new Request.Builder().url(base + path).header("accept", "application/json");
        if (body != null) rb.method(method, RequestBody.create(body.toString(), JSON));
        else rb.get();
        try (Response res = client.newCall(rb.build()).execute()) {
            String text = res.body() != null ? res.body().string() : "";
            JSONObject data;
            try { data = text.isEmpty() ? new JSONObject() : new JSONObject(text); }
            catch (JSONException e) { data = new JSONObject(); }
            if (!res.isSuccessful()) {
                if (res.code() == 401 && !path.startsWith("/api/auth")) clearSession();
                String msg = data.optString("error", "");
                if (msg.isEmpty()) msg = "요청을 처리하지 못했습니다. (HTTP " + res.code() + ")";
                throw new ApiException(res.code(), msg);
            }
            return data;
        } catch (IOException e) {
            throw new ApiException(0, "서버에 연결할 수 없습니다. 인터넷 연결을 확인해 주세요.");
        }
    }

    /** 단말 실시간 보기 WebSocket. 서버는 Origin이 있으면 같은 출처인지 확인하므로 서버 주소를 그대로 넣는다. */
    public WebSocket openLive(int deviceId, WebSocketListener listener) {
        String ws = base.replaceFirst("^http", "ws") + "/api/admin/devices/" + deviceId + "/live";
        Request req = new Request.Builder().url(ws).header("Origin", base).build();
        return wsClient.newWebSocket(req, listener);
    }
}
