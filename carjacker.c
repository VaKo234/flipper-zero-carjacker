#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    DemoModeMenu = 0,
    DemoModeRolling,
    DemoModeReplay,
    DemoModeRelay,
} DemoMode;

typedef enum {
    ReplayStageCapture = 0,
    ReplayStageReplay,
    ReplayStageDone,
} ReplayStage;

typedef struct {
    bool running;
    DemoMode mode;

    uint8_t menu_index;

    uint16_t rolling_fob_counter;
    uint16_t rolling_car_counter;
    uint16_t rolling_last_code;
    uint16_t rolling_last_tx_counter;
    bool rolling_tx_valid;

    uint16_t replay_fob_counter;
    uint16_t replay_car_counter;
    uint16_t replay_captured_code;
    uint16_t replay_captured_counter;
    ReplayStage replay_stage;
    bool replay_result_rejected;

    uint8_t relay_anim_frame;

    uint32_t shared_key;
    NotificationApp* notifications;
} RollingCodeDemoApp;

static uint16_t rolling_code_step(uint32_t counter, uint32_t key) {
    uint32_t state = (counter ^ key) & 0xFFFF;
    for(uint8_t i = 0; i < 8; i++) {
        uint32_t lsb = state & 1;
        state >>= 1;
        if(lsb) state ^= 0xB400;
    }
    return (uint16_t)(state & 0xFFFF);
}

static void reset_rolling_mode(RollingCodeDemoApp* app) {
    app->rolling_fob_counter = 40;
    app->rolling_car_counter = 40;
    app->rolling_last_code = 0;
    app->rolling_last_tx_counter = 0;
    app->rolling_tx_valid = false;
}

static void reset_replay_mode(RollingCodeDemoApp* app) {
    app->replay_fob_counter = 100;
    app->replay_car_counter = 100;
    app->replay_captured_code = 0;
    app->replay_captured_counter = 0;
    app->replay_stage = ReplayStageCapture;
    app->replay_result_rejected = false;
}

static void draw_menu(Canvas* canvas, const RollingCodeDemoApp* app) {
    static const char* options[] = {
        "How Rolling Codes Work",
        "Why Replay Fails",
        "Relay Attack Explained",
    };

    canvas_clear(canvas);
    elements_bold_rounded_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 11, AlignCenter, AlignBottom, "Rolling-Code Demo");

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < 3; i++) {
        const uint8_t y = 24 + (i * 12);
        if(app->menu_index == i) canvas_draw_str(canvas, 3, y, ">");
        canvas_draw_str(canvas, 10, y, options[i]);
    }

    canvas_draw_str(canvas, 2, 62, "OK=open  BACK=exit");
}

static void draw_rolling_mode(Canvas* canvas, const RollingCodeDemoApp* app) {
    char line[48];

    canvas_clear(canvas);
    elements_rounded_frame(canvas, 0, 0, 128, 64);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignBottom, "How Rolling Codes Work");

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "FOB CTR:%03u CODE:%04X", app->rolling_last_tx_counter, app->rolling_last_code);
    canvas_draw_str(canvas, 2, 22, line);

    snprintf(line, sizeof(line), "CAR NEXT CTR:%03u", app->rolling_car_counter);
    canvas_draw_str(canvas, 2, 32, line);

    canvas_draw_str(
        canvas,
        2,
        42,
        app->rolling_tx_valid ? "Result: ACCEPTED  UNLOCKED" : "Result: waiting for TX");

    canvas_draw_str(canvas, 2, 52, "OK=transmit next rolling code");
    canvas_draw_str(canvas, 2, 62, "BACK=menu");
}

static void draw_replay_mode(Canvas* canvas, const RollingCodeDemoApp* app) {
    char line[48];

    canvas_clear(canvas);
    elements_rounded_frame(canvas, 0, 0, 128, 64);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignBottom, "Why Replay Fails");

    canvas_set_font(canvas, FontSecondary);

    snprintf(line, sizeof(line), "Captured: CTR:%03u CODE:%04X", app->replay_captured_counter, app->replay_captured_code);
    canvas_draw_str(canvas, 2, 22, line);

    snprintf(line, sizeof(line), "Car expects CTR >= %03u", app->replay_car_counter);
    canvas_draw_str(canvas, 2, 32, line);

    if(app->replay_stage == ReplayStageCapture) {
        canvas_draw_str(canvas, 2, 42, "Step 1: Capture a valid unlock");
        canvas_draw_str(canvas, 2, 52, "OK=capture code");
    } else if(app->replay_stage == ReplayStageReplay) {
        canvas_draw_str(canvas, 2, 42, "Step 2: Replay captured code");
        canvas_draw_str(canvas, 2, 52, "OK=replay captured packet");
    } else {
        canvas_draw_str(
            canvas,
            2,
            42,
            app->replay_result_rejected ? "Replay result: REJECTED" : "Replay result: ACCEPTED");
        canvas_draw_str(canvas, 2, 52, "Counter-based codes stop replay");
    }

    canvas_draw_str(canvas, 2, 62, "BACK=menu  OK=next/reset");
}

static void draw_relay_mode(Canvas* canvas, const RollingCodeDemoApp* app) {
    canvas_clear(canvas);
    elements_rounded_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignBottom, "Relay Attack Concept");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_frame(canvas, 2, 16, 36, 20);
    canvas_draw_frame(canvas, 90, 16, 36, 20);

    canvas_draw_str(canvas, 4, 24, "Owner");
    canvas_draw_str(canvas, 4, 32, "+ Fob");

    canvas_draw_str(canvas, 92, 24, "Car");
    canvas_draw_str(canvas, 92, 32, "ECU");

    uint8_t phase = app->relay_anim_frame % 24;
    uint8_t relay_x = 38 + (phase * 2);
    if(relay_x > 89) relay_x = 89;

    canvas_draw_line(canvas, 38, 26, 89, 26);
    canvas_draw_dot(canvas, relay_x, 26);

    canvas_draw_str(canvas, 2, 44, "Attackers relay in real-time");
    canvas_draw_str(canvas, 2, 52, "Car thinks fob is nearby");
    canvas_draw_str(canvas, 2, 62, "Mitigation: UWB + motion");
}

static void draw_callback(Canvas* canvas, void* context) {
    RollingCodeDemoApp* app = context;

    switch(app->mode) {
    case DemoModeMenu:
        draw_menu(canvas, app);
        break;
    case DemoModeRolling:
        draw_rolling_mode(canvas, app);
        break;
    case DemoModeReplay:
        draw_replay_mode(canvas, app);
        break;
    case DemoModeRelay:
        draw_relay_mode(canvas, app);
        break;
    }
}

static void handle_ok_in_rolling(RollingCodeDemoApp* app) {
    app->rolling_fob_counter++;
    app->rolling_last_tx_counter = app->rolling_fob_counter;
    app->rolling_last_code = rolling_code_step(app->rolling_last_tx_counter, app->shared_key);

    bool in_window =
        (app->rolling_last_tx_counter >= app->rolling_car_counter) &&
        (app->rolling_last_tx_counter <= (uint16_t)(app->rolling_car_counter + 16));

    app->rolling_tx_valid = in_window;
    if(in_window) {
        app->rolling_car_counter = app->rolling_last_tx_counter + 1;
        notification_message(app->notifications, &sequence_success);
    } else {
        notification_message(app->notifications, &sequence_error);
    }
}

static void handle_ok_in_replay(RollingCodeDemoApp* app) {
    if(app->replay_stage == ReplayStageCapture) {
        app->replay_fob_counter++;
        app->replay_captured_counter = app->replay_fob_counter;
        app->replay_captured_code = rolling_code_step(app->replay_captured_counter, app->shared_key);

        app->replay_car_counter = app->replay_captured_counter + 1;
        app->replay_stage = ReplayStageReplay;
        notification_message(app->notifications, &sequence_single_vibro);
    } else if(app->replay_stage == ReplayStageReplay) {
        bool replay_accepted =
            (app->replay_captured_counter >= app->replay_car_counter) &&
            (app->replay_captured_counter <= (uint16_t)(app->replay_car_counter + 16));

        app->replay_result_rejected = !replay_accepted;
        app->replay_stage = ReplayStageDone;

        if(replay_accepted) {
            notification_message(app->notifications, &sequence_success);
        } else {
            notification_message(app->notifications, &sequence_error);
        }
    } else {
        reset_replay_mode(app);
    }
}

static void input_callback(InputEvent* event, void* context) {
    RollingCodeDemoApp* app = context;

    if(event->type != InputTypeShort) return;

    if(app->mode == DemoModeMenu) {
        if(event->key == InputKeyUp && app->menu_index > 0) {
            app->menu_index--;
        } else if(event->key == InputKeyDown && app->menu_index < 2) {
            app->menu_index++;
        } else if(event->key == InputKeyOk) {
            if(app->menu_index == 0) {
                reset_rolling_mode(app);
                app->mode = DemoModeRolling;
            } else if(app->menu_index == 1) {
                reset_replay_mode(app);
                app->mode = DemoModeReplay;
            } else {
                app->relay_anim_frame = 0;
                app->mode = DemoModeRelay;
            }
        } else if(event->key == InputKeyBack) {
            app->running = false;
        }
        return;
    }

    if(event->key == InputKeyBack) {
        app->mode = DemoModeMenu;
        return;
    }

    if(event->key == InputKeyOk) {
        if(app->mode == DemoModeRolling) {
            handle_ok_in_rolling(app);
        } else if(app->mode == DemoModeReplay) {
            handle_ok_in_replay(app);
        }
    }
}

int32_t carjacker_app(void* p) {
    (void)p;

    RollingCodeDemoApp* app = malloc(sizeof(RollingCodeDemoApp));
    if(!app) return -1;

    app->running = true;
    app->mode = DemoModeMenu;
    app->menu_index = 0;
    app->relay_anim_frame = 0;
    app->shared_key = 0xD3A5C79B;
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    reset_rolling_mode(app);
    reset_replay_mode(app);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, app);
    view_port_input_callback_set(view_port, input_callback, app);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    while(app->running) {
        if(app->mode == DemoModeRelay) {
            app->relay_anim_frame++;
        }

        view_port_update(view_port);
        furi_delay_ms(80);
    }

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);

    return 0;
}
