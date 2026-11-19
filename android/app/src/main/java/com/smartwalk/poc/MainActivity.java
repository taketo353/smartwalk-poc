package com.smartwalk.poc;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayInputStream;
import java.net.URI;
import java.net.URISyntaxException;

public final class MainActivity extends Activity {
    private static final String PREF_SERVER = "server";
    private static final String STUDENT = "/student";
    private static final String ADMIN = "/admin";
    private static final int INK = Color.rgb(19, 52, 54);
    private static final int BACKGROUND = Color.rgb(241, 246, 243);

    private volatile ServerAddress endpoint;
    private SharedPreferences preferences;
    private WebView browser;
    private ScrollView notice;
    private TextView connection;
    private TextView noticeHeading;
    private TextView noticeMessage;
    private Button noticeAction;
    private String screen = STUDENT;
    private String loadingUrl;
    private boolean pageFailed;
    private boolean wasPaused;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        preferences = getSharedPreferences("smartwalk", MODE_PRIVATE);
        endpoint = ServerAddress.parse(preferences.getString(PREF_SERVER, ""));
        if (state != null && ADMIN.equals(state.getString("screen"))) screen = ADMIN;
        setContentView(buildLayout());
        openCurrentScreen();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putString("screen", screen);
    }

    @Override
    protected void onPause() {
        super.onPause();
        wasPaused = true;
        browser.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        browser.onResume();
        if (wasPaused) openCurrentScreen();
        wasPaused = false;
    }

    @Override
    protected void onDestroy() {
        browser.stopLoading();
        ((FrameLayout) browser.getParent()).removeView(browser);
        browser.destroy();
        super.onDestroy();
    }

    private LinearLayout buildLayout() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BACKGROUND);
        root.setOnApplyWindowInsetsListener((view, windowInsets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = windowInsets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else {
                view.setPadding(windowInsets.getSystemWindowInsetLeft(), windowInsets.getSystemWindowInsetTop(),
                        windowInsets.getSystemWindowInsetRight(), windowInsets.getSystemWindowInsetBottom());
            }
            return windowInsets;
        });

        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(16), dp(3), dp(8), dp(3));
        bar.setBackgroundColor(Color.rgb(228, 240, 237));
        connection = text(13);
        bar.addView(connection, new LinearLayout.LayoutParams(0, -2, 1));
        Button settings = button(R.string.settings);
        settings.setOnClickListener(view -> showSettings());
        bar.addView(settings);
        root.addView(bar, new LinearLayout.LayoutParams(-1, -2));

        FrameLayout body = new FrameLayout(this);
        browser = new WebView(this);
        browser.setBackgroundColor(BACKGROUND);
        WebSettings web = browser.getSettings();
        web.setJavaScriptEnabled(true);
        web.setAllowFileAccess(false);
        web.setAllowContentAccess(false);
        web.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        web.setCacheMode(WebSettings.LOAD_NO_CACHE);
        browser.setWebViewClient(new SmartWalkClient());
        body.addView(browser, new FrameLayout.LayoutParams(-1, -1));

        notice = new ScrollView(this);
        notice.setFillViewport(true);
        notice.setBackgroundColor(BACKGROUND);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER_VERTICAL);
        panel.setPadding(dp(24), dp(24), dp(24), dp(24));
        TextView brand = text(17);
        brand.setText(R.string.app_name);
        panel.addView(brand);
        noticeHeading = text(32);
        noticeHeading.setPadding(0, dp(18), 0, dp(8));
        panel.addView(noticeHeading);
        noticeMessage = text(17);
        noticeMessage.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        panel.addView(noticeMessage);
        noticeAction = button(R.string.settings);
        LinearLayout.LayoutParams actionSize = new LinearLayout.LayoutParams(-2, -2);
        actionSize.topMargin = dp(12);
        panel.addView(noticeAction, actionSize);
        notice.addView(panel, new ScrollView.LayoutParams(-1, -2));
        body.addView(notice, new FrameLayout.LayoutParams(-1, -1));
        root.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
        return root;
    }

    private void openCurrentScreen() {
        browser.stopLoading();
        pageFailed = false;
        if (endpoint == null) {
            loadingUrl = null;
            connection.setText(R.string.unnamed_server);
            showNotice(R.string.unnamed_server, R.string.connect_server, R.string.settings, this::showSettings);
            return;
        }
        connection.setText(endpoint.toString());
        loadingUrl = endpoint.urlFor(screen);
        showNotice(R.string.app_name, R.string.loading, 0, null);
        browser.loadUrl(loadingUrl);
    }

    private void showNotice(int heading, int message, int action, Runnable callback) {
        browser.setVisibility(View.INVISIBLE);
        notice.setVisibility(View.VISIBLE);
        noticeHeading.setText(heading);
        noticeMessage.setText(message);
        noticeAction.setVisibility(callback == null ? View.GONE : View.VISIBLE);
        if (callback != null) {
            noticeAction.setText(action);
            noticeAction.setOnClickListener(view -> callback.run());
        }
    }

    private void showSettings() {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(24), dp(12), dp(24), 0);
        TextView help = text(15);
        help.setText(R.string.settings_details);
        form.addView(help);
        EditText input = new EditText(this);
        input.setSingleLine();
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        input.setHint(R.string.url_example);
        input.setContentDescription(getString(R.string.settings));
        if (endpoint != null) input.setText(endpoint.toString());
        form.addView(input, new LinearLayout.LayoutParams(-1, -2));
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(R.string.settings).setView(form)
                .setNegativeButton(R.string.cancel, null).setPositiveButton(R.string.save, null)
                .create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            ServerAddress address = ServerAddress.parse(input.getText().toString());
            if (address == null) {
                input.setError(getString(R.string.invalid_endpoint));
                return;
            }
            endpoint = address;
            preferences.edit().putString(PREF_SERVER, address.toString()).apply();
            dialog.dismiss();
            openCurrentScreen();
        }));
        dialog.show();
    }

    private TextView text(int size) {
        TextView text = new TextView(this);
        text.setTextSize(size);
        text.setTextColor(INK);
        return text;
    }

    private Button button(int label) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        return button;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static URI uri(String url) {
        try {
            return new URI(url);
        } catch (URISyntaxException error) {
            return null;
        }
    }

    private boolean canOpenPage(URI address) {
        ServerAddress allowed = endpoint;
        if (address == null || allowed == null || !allowed.allows(address)) return false;
        String path = address.getPath();
        return STUDENT.equals(path) || ADMIN.equals(path) || "/".equals(path) || "".equals(path);
    }

    private final class SmartWalkClient extends WebViewClient {
        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            return !canOpenPage(uri(request.getUrl().toString()));
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            URI target = uri(request.getUrl().toString());
            ServerAddress allowed = endpoint;
            if (target != null && allowed != null && allowed.allows(target)) return null;
            return new WebResourceResponse("text/plain", "UTF-8",
                    new ByteArrayInputStream(new byte[0]));
        }

        @Override
        public void onPageStarted(WebView view, String url, android.graphics.Bitmap icon) {
            URI target = uri(url);
            if (!canOpenPage(target)) {
                view.stopLoading();
                return;
            }
            screen = ADMIN.equals(target.getPath()) ? ADMIN : STUDENT;
            loadingUrl = url;
            pageFailed = false;
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            if (url.equals(loadingUrl) && !pageFailed) {
                notice.setVisibility(View.GONE);
                browser.setVisibility(View.VISIBLE);
            }
        }

        @Override
        public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
            showFailure(request);
        }

        @Override
        public void onReceivedHttpError(WebView view, WebResourceRequest request, WebResourceResponse response) {
            showFailure(request);
        }

        private void showFailure(WebResourceRequest request) {
            if (request.isForMainFrame() && request.getUrl().toString().equals(loadingUrl)) {
                pageFailed = true;
                showNotice(R.string.unknown, R.string.unreachable, R.string.retry, MainActivity.this::openCurrentScreen);
            }
        }
    }
}
