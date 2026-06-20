#include <furi.h>
#include <gui/canvas.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <stdlib.h>

#include "daikin64_ac_remote_icons.h"
#include "../../lib/hvac_daikin64/hvac_daikin64.h"

typedef enum {
    Daikin64ViewRemote,
} Daikin64View;

typedef enum {
    Daikin64ButtonPower,
    Daikin64ButtonMode,
    Daikin64ButtonTempUp,
    Daikin64ButtonFan,
    Daikin64ButtonTempDown,
    Daikin64ButtonSwing,
    Daikin64ButtonCount,
} Daikin64Button;

typedef struct {
    Daikin64State state;
    uint8_t selected;
    uint8_t mode_index;
    uint8_t fan_index;
} Daikin64RemoteModel;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    View* view;
} Daikin64App;

typedef struct {
    uint8_t value;
    const Icon* icon;
    const Icon* selected_icon;
} Daikin64IconOption;

static const Daikin64IconOption daikin64_modes[] = {
    {Daikin64ModeAuto, &I_auto_19x20, &I_auto_hover_19x20},
    {Daikin64ModeCool, &I_cold_19x20, &I_cold_hover_19x20},
    {Daikin64ModeHeat, &I_heat_19x20, &I_heat_hover_19x20},
    {Daikin64ModeDry, &I_dry_19x20, &I_dry_hover_19x20},
    {Daikin64ModeFan, &I_fan_speed_4_19x20, &I_fan_speed_4_hover_19x20},
};

static const Daikin64IconOption daikin64_fans[] = {
    {Daikin64FanLow, &I_fan_speed_1_19x20, &I_fan_speed_1_hover_19x20},
    {Daikin64FanMedium, &I_fan_speed_2_19x20, &I_fan_speed_2_hover_19x20},
    {Daikin64FanHigh, &I_fan_speed_3_19x20, &I_fan_speed_3_hover_19x20},
    {Daikin64FanTurbo, &I_fan_speed_4_19x20, &I_fan_speed_4_hover_19x20},
    {Daikin64FanAuto, &I_auto_19x20, &I_auto_hover_19x20},
};

static void
    daikin64_send_with_feedback(Daikin64App* app, const Daikin64State* state, bool power_toggle) {
    notification_message(app->notifications, &sequence_blink_white_100);
    if(power_toggle) {
        daikin64_send_power_toggle(state);
    } else {
        daikin64_send(state);
    }
    notification_message(app->notifications, &sequence_blink_stop);
}

static void daikin64_draw_button(
    Canvas* canvas,
    const Daikin64RemoteModel* model,
    Daikin64Button button,
    uint8_t x,
    uint8_t y,
    const Icon* icon,
    const Icon* selected_icon) {
    canvas_draw_icon(canvas, x, y, model->selected == button ? selected_icon : icon);
}

static void daikin64_draw_callback(Canvas* canvas, void* context) {
    Daikin64RemoteModel* model = context;
    char temperature[4];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    daikin64_draw_button(
        canvas,
        model,
        Daikin64ButtonPower,
        6,
        17,
        &I_off_19x20,
        &I_off_hover_19x20);
    canvas_draw_icon(canvas, 9, 39, &I_off_text_14x5);

    daikin64_draw_button(
        canvas,
        model,
        Daikin64ButtonMode,
        39,
        17,
        daikin64_modes[model->mode_index].icon,
        daikin64_modes[model->mode_index].selected_icon);
    canvas_draw_icon(canvas, 39, 39, &I_mode_text_20x5);

    canvas_draw_icon(canvas, 0, 63, &I_frame_30x39);
    daikin64_draw_button(
        canvas,
        model,
        Daikin64ButtonTempUp,
        3,
        51,
        &I_tempup_24x21,
        &I_tempup_hover_24x21);
    daikin64_draw_button(
        canvas,
        model,
        Daikin64ButtonTempDown,
        3,
        93,
        &I_tempdown_24x21,
        &I_tempdown_hover_24x21);

    snprintf(temperature, sizeof(temperature), "%u", model->state.temperature);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 4, 86, temperature);

    daikin64_draw_button(
        canvas,
        model,
        Daikin64ButtonFan,
        39,
        54,
        daikin64_fans[model->fan_index].icon,
        daikin64_fans[model->fan_index].selected_icon);
    canvas_draw_icon(canvas, 41, 76, &I_fan_text_14x5);

    daikin64_draw_button(
        canvas,
        model,
        Daikin64ButtonSwing,
        39,
        91,
        model->state.swing ? &I_vane_auto_move_19x20 : &I_vane_h1_19x20,
        model->state.swing ? &I_vane_auto_move_hover_19x20 : &I_vane_h1_hover_19x20);
    canvas_draw_icon(canvas, 38, 113, &I_vane_text_20x5);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 6, 11, "Daikin64");
}

static void daikin64_move_selection(Daikin64RemoteModel* model, InputKey key) {
    switch(key) {
    case InputKeyUp:
        if(model->selected >= 2) model->selected -= 2;
        break;
    case InputKeyDown:
        if(model->selected + 2 < Daikin64ButtonCount) model->selected += 2;
        break;
    case InputKeyLeft:
        if(model->selected % 2) model->selected--;
        break;
    case InputKeyRight:
        if((model->selected % 2) == 0) model->selected++;
        break;
    default:
        break;
    }
}

static bool daikin64_input_callback(InputEvent* event, void* context) {
    Daikin64App* app = context;
    Daikin64State send_state;
    bool should_send = false;
    bool power_toggle = false;

    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyOk) {
        with_view_model(
            app->view,
            Daikin64RemoteModel * model,
            {
                switch(model->selected) {
                case Daikin64ButtonPower:
                    model->state.power = !model->state.power;
                    power_toggle = true;
                    break;
                case Daikin64ButtonMode:
                    model->mode_index = (model->mode_index + 1) % COUNT_OF(daikin64_modes);
                    model->state.mode = daikin64_modes[model->mode_index].value;
                    break;
                case Daikin64ButtonTempUp:
                    if(model->state.temperature < DAIKIN64_MAX_TEMP) model->state.temperature++;
                    break;
                case Daikin64ButtonTempDown:
                    if(model->state.temperature > DAIKIN64_MIN_TEMP) model->state.temperature--;
                    break;
                case Daikin64ButtonFan:
                    model->fan_index = (model->fan_index + 1) % COUNT_OF(daikin64_fans);
                    model->state.fan = daikin64_fans[model->fan_index].value;
                    break;
                case Daikin64ButtonSwing:
                    model->state.swing = !model->state.swing;
                    break;
                default:
                    break;
                }

                send_state = model->state;
                if(!power_toggle) send_state.power = false;
                should_send = true;
            },
            true);

        if(should_send) daikin64_send_with_feedback(app, &send_state, power_toggle);
        return true;
    }

    with_view_model(
        app->view,
        Daikin64RemoteModel * model,
        { daikin64_move_selection(model, event->key); },
        true);

    return true;
}

static bool daikin64_navigation_callback(void* context) {
    Daikin64App* app = context;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static Daikin64App* daikin64_app_alloc(void) {
    Daikin64App* app = malloc(sizeof(Daikin64App));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->view = view_alloc();

    view_set_orientation(app->view, ViewOrientationVertical);
    view_set_context(app->view, app);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(Daikin64RemoteModel));
    view_set_draw_callback(app->view, daikin64_draw_callback);
    view_set_input_callback(app->view, daikin64_input_callback);

    with_view_model(
        app->view,
        Daikin64RemoteModel * model,
        {
            daikin64_init(&model->state);
            model->selected = Daikin64ButtonPower;
            model->mode_index = 1;
            model->fan_index = 0;
            model->state.mode = daikin64_modes[model->mode_index].value;
            model->state.fan = daikin64_fans[model->fan_index].value;
        },
        true);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, daikin64_navigation_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->view_dispatcher, Daikin64ViewRemote, app->view);

    return app;
}

static void daikin64_app_free(Daikin64App* app) {
    view_dispatcher_remove_view(app->view_dispatcher, Daikin64ViewRemote);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t daikin64_ac_remote_app(void* p) {
    UNUSED(p);

    Daikin64App* app = daikin64_app_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, Daikin64ViewRemote);
    view_dispatcher_run(app->view_dispatcher);
    daikin64_app_free(app);

    return 0;
}
