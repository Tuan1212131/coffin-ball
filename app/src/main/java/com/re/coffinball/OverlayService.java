package com.re.coffinball;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.Toast;

public class OverlayService extends Service {
    private static final String SVC = "/sdcard/MT2/mcp/c16svc.sh";
    private WindowManager wm;
    private View ball;
    private WindowManager.LayoutParams lp;
    private Handler h = new Handler(Looper.getMainLooper());
    private volatile boolean running = false;
    private int startX, startY;
    private float startTouchX, startTouchY;

    @Override public IBinder onBind(Intent i) { return null; }

    @Override public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            NotificationChannel ch = new NotificationChannel("cb", "棺椁助手", NotificationManager.IMPORTANCE_LOW);
            nm.createNotificationChannel(ch);
            Notification n = new Notification.Builder(this, "cb")
                    .setContentTitle("棺椁助手")
                    .setContentText("点悬浮球 开/关 自动发力")
                    .setSmallIcon(android.R.drawable.ic_menu_compass)
                    .build();
            startForeground(1, n);
        }
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        addBall();
    }

    private void addBall() {
        ball = new TextView(this);
        ball.setText("棺");
        ball.setTextColor(Color.WHITE);
        ball.setTextSize(18);
        ball.setGravity(Gravity.CENTER);
        android.graphics.drawable.GradientDrawable g = new android.graphics.drawable.GradientDrawable();
        g.setColor(0xFF555555); g.setCornerRadius(dp(30));
        ball.setBackground(g);
        int s = dp(56);
        lp = new WindowManager.LayoutParams(s, s,
                Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                            : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.TOP | Gravity.END;
        lp.x = dp(8); lp.y = dp(180);
        ball.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent ev) {
                switch (ev.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        startX = lp.x; startY = lp.y;
                        startTouchX = ev.getRawX(); startTouchY = ev.getRawY();
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        lp.x = startX + (int)(ev.getRawX() - startTouchX);
                        lp.y = startY + (int)(ev.getRawY() - startTouchY);
                        try { wm.updateViewLayout(ball, lp); } catch (Exception e) {}
                        return true;
                    case MotionEvent.ACTION_UP:
                        if (Math.abs(ev.getRawX() - startTouchX) < dp(6) &&
                            Math.abs(ev.getRawY() - startTouchY) < dp(6)) toggle();
                        return true;
                }
                return false;
            }
        });
        try { wm.addView(ball, lp); } catch (Exception e) {
            Toast.makeText(this, "悬浮窗权限未授予", Toast.LENGTH_LONG).show();
            stopSelf();
        }
    }

    private int dp(int v) { return (int)(v * getResources().getDisplayMetrics().density); }

    private void toggle() {
        running = !running;
        String cmd = running ? "start" : "stop";
        final String tip = running ? "开(启动引擎)…" : "关(停止)…";
        setColor(running ? 0xFF2E7D32 : 0xFF555555);
        Toast.makeText(this, tip, Toast.LENGTH_SHORT).show();
        new Thread(new Runnable() {
            @Override public void run() {
                exec("sh " + SVC + " " + cmd);
                h.postDelayed(new Runnable() {
                    @Override public void run() {
                        final boolean on = isRunning();
                        running = on;
                        h.post(new Runnable() {
                            @Override public void run() {
                                setColor(on ? 0xFF2E7D32 : 0xFF555555);
                                Toast.makeText(OverlayService.this, on ? "引擎运行中" : "引擎已停", Toast.LENGTH_SHORT).show();
                            }
                        });
                    }
                }, 1800);
            }
        }).start();
    }

    private boolean isRunning() {
        try {
            Process p = new ProcessBuilder("su", "-c", "pgrep -f coffin_auto_second_open_v16_BEST_2p05s_55pk.py").start();
            java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(p.getInputStream()));
            String line = r.readLine();
            p.destroy();
            return line != null && line.trim().length() > 0;
        } catch (Exception e) { return false; }
    }

    private void exec(String cmd) {
        try {
            new ProcessBuilder("su", "-c", cmd).redirectErrorStream(true).start();
        } catch (Exception e) { /* root denied or error */ }
    }

    private void setColor(int c) {
        if (ball == null) return;
        android.graphics.drawable.GradientDrawable g = (android.graphics.drawable.GradientDrawable) ball.getBackground();
        if (g != null) g.setColor(c);
    }

    @Override public void onDestroy() {
        if (ball != null) try { wm.removeView(ball); } catch (Exception e) {}
        super.onDestroy();
    }
}
