package com.audreaming.labadmin;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/** 자식 뷰를 가로로 놓다가 자리가 모자라면 다음 줄로 넘기는 간단한 배치. */
public class FlowLayout extends ViewGroup {
    private final int hGap, vGap;

    public FlowLayout(Context c, int hGap, int vGap) {
        super(c);
        this.hGap = hGap;
        this.vGap = vGap;
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int maxW = MeasureSpec.getSize(widthSpec) - getPaddingLeft() - getPaddingRight();
        if (MeasureSpec.getMode(widthSpec) == MeasureSpec.UNSPECIFIED) maxW = Integer.MAX_VALUE;
        int x = 0, y = 0, lineH = 0, usedW = 0;
        for (int i = 0; i < getChildCount(); i++) {
            View ch = getChildAt(i);
            if (ch.getVisibility() == GONE) continue;
            measureChild(ch, MeasureSpec.makeMeasureSpec(maxW, MeasureSpec.AT_MOST), MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
            int w = ch.getMeasuredWidth(), h = ch.getMeasuredHeight();
            if (x > 0 && x + w > maxW) { x = 0; y += lineH + vGap; lineH = 0; }
            x += w + hGap;
            usedW = Math.max(usedW, x - hGap);
            lineH = Math.max(lineH, h);
        }
        int width = MeasureSpec.getMode(widthSpec) == MeasureSpec.EXACTLY ? MeasureSpec.getSize(widthSpec) : usedW + getPaddingLeft() + getPaddingRight();
        setMeasuredDimension(width, y + lineH + getPaddingTop() + getPaddingBottom());
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int maxW = r - l - getPaddingLeft() - getPaddingRight();
        int x = 0, y = 0, lineH = 0;
        for (int i = 0; i < getChildCount(); i++) {
            View ch = getChildAt(i);
            if (ch.getVisibility() == GONE) continue;
            int w = ch.getMeasuredWidth(), h = ch.getMeasuredHeight();
            if (x > 0 && x + w > maxW) { x = 0; y += lineH + vGap; lineH = 0; }
            ch.layout(getPaddingLeft() + x, getPaddingTop() + y, getPaddingLeft() + x + w, getPaddingTop() + y + h);
            x += w + hGap;
            lineH = Math.max(lineH, h);
        }
    }
}
