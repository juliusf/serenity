#include "PianoWidget.h"
#include <AK/Queue.h>
#include <LibDraw/GraphicsBitmap.h>
#include <LibGUI/GPainter.h>
#include <math.h>

PianoWidget::PianoWidget()
{
    memset(keys, 0, sizeof(keys));
    m_front_buffer = new Sample[2048];
    m_back_buffer = new Sample[2048];

    set_font(Font::default_fixed_width_font());
}

PianoWidget::~PianoWidget()
{
}

void PianoWidget::paint_event(GPaintEvent& event)
{
    GPainter painter(*this);
    painter.add_clip_rect(event.rect());

    painter.fill_rect(event.rect(), Color::Black);

    auto* samples = m_front_buffer;
    Color wave_color;
    if (m_wave_type == WaveType::Sine)
        wave_color = Color(255, 192, 0);
    else if (m_wave_type == WaveType::Saw)
        wave_color = Color(240, 100, 128);
    else if (m_wave_type == WaveType::Square)
        wave_color = Color(128, 160, 255);

    int prev_x = 0;
    int prev_y = m_height / 2;
    for (int x = 0; x < m_sample_count; ++x) {
        double val = samples[x].left;
        val /= 32768;
        val *= m_height * 2;
        int y = (m_height / 2) + val;
        if (x == 0)
            painter.set_pixel({ x, y }, wave_color);
        else
            painter.draw_line({ prev_x, prev_y }, { x, y }, wave_color);
        prev_x = x;
        prev_y = y;
    }

    render_piano(painter);
    render_knobs(painter);
}

void PianoWidget::fill_audio_buffer(uint8_t* stream, int len)
{
    m_sample_count = len / sizeof(Sample);
    memset(stream, 0, len);

    auto* sst = (Sample*)stream;
    for (int i = 0; i < m_sample_count; ++i) {
        static const double volume = 1800;
        for (size_t n = 0; n < (sizeof(m_note_on) / sizeof(bool)); ++n) {
            if (!m_note_on[n])
                continue;
            double val = 0;
            if (m_wave_type == WaveType::Sine)
                val = ((volume * m_power[n]) * w_sine(n));
            else if (m_wave_type == WaveType::Saw)
                val = ((volume * m_power[n]) * w_saw(n));
            else if (m_wave_type == WaveType::Square)
                val = ((volume * m_power[n]) * w_square(n));
            if (sst[i].left == 0)
                sst[i].left = val;
            else
                sst[i].left += val;
        }
        sst[i].right = sst[i].left;
    }

    // Release pressed notes.
    if (m_release_enabled) {
        for (size_t n = 0; n < (sizeof(m_note_on) / sizeof(bool)); ++n) {
            if (m_note_on[n])
                m_power[n] *= 0.965;
        }
    }

    static Queue<Sample*> delay_frames;
    static const int delay_length_in_frames = 50;

    if (m_delay_enabled) {
        if (delay_frames.size() >= delay_length_in_frames) {
            auto* to_blend = delay_frames.dequeue();
            for (int i = 0; i < m_sample_count; ++i) {
                sst[i].left += to_blend[i].left * 0.333333;
                sst[i].right += to_blend[i].right * 0.333333;
            }
            delete[] to_blend;
        }
        Sample* frame = new Sample[m_sample_count];
        memcpy(frame, sst, m_sample_count * sizeof(Sample));

        delay_frames.enqueue(frame);
    }

    ASSERT(len <= 2048 * (int)sizeof(Sample));
    memcpy(m_back_buffer, (Sample*)stream, len);
    swap(m_front_buffer, m_back_buffer);
}

double PianoWidget::w_sine(size_t n)
{
    double pos = note_frequency[n] / 44100.0;
    double sin_step = pos * 2 * M_PI;
    double w = sin(m_sin_pos[n]);
    m_sin_pos[n] += sin_step;
    return w;
}

static inline double hax_floor(double t)
{
    return (int)t;
}

double PianoWidget::w_saw(size_t n)
{
    double saw_step = note_frequency[n] / 44100.0;
    double t = m_saw_pos[n];
    double w = (0.5 - (t - hax_floor(t))) * 2;
    //printf("w: %g, step: %g\n", w, saw_step);
    m_saw_pos[n] += saw_step;
    return w;
}

double PianoWidget::w_square(size_t n)
{
    double pos = note_frequency[n] / 44100.0;
    double square_step = pos * 2 * M_PI;
    double w = sin(m_square_pos[n]);
    if (w > 0)
        w = 1;
    else
        w = -1;
    //printf("w: %g, step: %g\n", w, square_step);
    m_square_pos[n] += square_step;
    return w;
}

int PianoWidget::octave_base() const
{
    return (m_octave - m_octave_min) * 12;
}

void PianoWidget::note(PianoKey offset_n, KeyCode key_code)
{
    bool is_down = keys[key_code] || (m_mouse_pressed && m_piano_key_under_mouse == offset_n);

    int n = offset_n + octave_base();
    if (m_note_on[n] == is_down)
        return;
    m_note_on[n] = is_down;
    m_sin_pos[n] = 0;
    m_saw_pos[n] = 0;
    if (is_down)
        m_power[n] = 1;
    //printf("note[%u] = %u (%g)\n", (unsigned)n, is_down, note_frequency[n]);
}

struct KeyDefinition {
    int index;
    PianoKey piano_key;
    String label;
    KeyCode key_code;
};

const KeyDefinition key_definitions[] = {
    { 0, K_C1, "A", KeyCode::Key_A },
    { 1, K_D1, "S", KeyCode::Key_S },
    { 2, K_E1, "D", KeyCode::Key_D },
    { 3, K_F1, "F", KeyCode::Key_F },
    { 4, K_G1, "G", KeyCode::Key_G },
    { 5, K_A1, "H", KeyCode::Key_H },
    { 6, K_B1, "J", KeyCode::Key_J },
    { 7, K_C2, "K", KeyCode::Key_K },
    { 8, K_D2, "L", KeyCode::Key_L },
    { 9, K_E2, ";", KeyCode::Key_Semicolon },
    { 10, K_F2, "'", KeyCode::Key_Apostrophe },
    { 11, K_G2, "r", KeyCode::Key_Return },
    { 0, K_Db1, "W", KeyCode::Key_W },
    { 1, K_Eb1, "E", KeyCode::Key_E },
    { 3, K_Gb1, "T", KeyCode::Key_T },
    { 4, K_Ab1, "Y", KeyCode::Key_Y },
    { 5, K_Bb1, "U", KeyCode::Key_U },
    { 7, K_Db2, "O", KeyCode::Key_O },
    { 8, K_Eb2, "P", KeyCode::Key_P },
    { 10, K_Gb2, "]", KeyCode::Key_RightBracket },
};

void PianoWidget::update_keys()
{
    for (auto& kd : key_definitions)
        note(kd.piano_key, kd.key_code);
}

void PianoWidget::keydown_event(GKeyEvent& event)
{
    switch (event.key()) {
    case KeyCode::Key_C:

        if (++m_wave_type == InvalidWave)
            m_wave_type = 0;
        break;
    case KeyCode::Key_V:
        m_delay_enabled = !m_delay_enabled;
        break;
    case KeyCode::Key_B:
        m_release_enabled = !m_release_enabled;
        break;
    case KeyCode::Key_Z:
        if (m_octave > m_octave_min)
            --m_octave;
        memset(m_note_on, 0, sizeof(m_note_on));
        break;
    case KeyCode::Key_X:
        if (m_octave < m_octave_max)
            ++m_octave;
        memset(m_note_on, 0, sizeof(m_note_on));
        break;
    }

    keys[event.key()] = true;
    update_keys();
    update();
}

void PianoWidget::keyup_event(GKeyEvent& event)
{
    keys[event.key()] = false;
    update_keys();
    update();
}

void PianoWidget::mousedown_event(GMouseEvent& event)
{
    m_mouse_pressed = true;

    m_piano_key_under_mouse = find_key_for_relative_position(event.x() - x(), event.y() - y());

    update_keys();
    update();
}

void PianoWidget::mouseup_event(GMouseEvent&)
{
    m_mouse_pressed = false;

    update_keys();
    update();
}

void PianoWidget::mousemove_event(GMouseEvent& event)
{
    if (!m_mouse_pressed)
        return;

    int mouse_was_over = m_piano_key_under_mouse;

    m_piano_key_under_mouse = find_key_for_relative_position(event.x() - x(), event.y() - y());

    if (m_piano_key_under_mouse == mouse_was_over)
        return;

    update_keys();
    update();
}

static int white_key_width = 22;
static int white_key_height = 60;
static int black_key_width = 16;
static int black_key_height = 35;
static int black_key_stride = white_key_width - black_key_width;
static int black_key_offset = white_key_width - black_key_width / 2;

Rect PianoWidget::define_piano_key_rect(int index, PianoKey n) const
{
    Rect rect;
    int stride = 0;
    int offset = 0;
    if (is_white(n)) {
        rect.set_width(white_key_width);
        rect.set_height(white_key_height);
    } else {
        rect.set_width(black_key_width);
        rect.set_height(black_key_height);
        stride = black_key_stride;
        offset = black_key_offset;
    }
    rect.set_x(offset + index * rect.width() + (index * stride));
    rect.set_y(m_height - white_key_height);
    return rect;
}

PianoKey PianoWidget::find_key_for_relative_position(int x, int y) const
{
    // here we iterate backwards because we want to try to match the black
    // keys first, which are defined last
    for (int i = (sizeof(key_definitions) / sizeof(KeyDefinition)) - 1; i >= 0; i--) {
        auto& kd = key_definitions[i];

        auto rect = define_piano_key_rect(kd.index, kd.piano_key);

        if (rect.contains(x, y))
            return kd.piano_key;
    }

    return K_None;
}

void PianoWidget::render_piano_key(GPainter& painter, int index, PianoKey n, const StringView& text)
{
    Color color;
    if (m_note_on[octave_base() + n]) {
        color = Color(64, 64, 255);
    } else {
        if (is_white(n))
            color = Color::White;
        else
            color = Color::Black;
    }

    auto rect = define_piano_key_rect(index, n);

    painter.fill_rect(rect, color);
    painter.draw_rect(rect, Color::Black);

    Color text_color;
    if (is_white(n)) {
        text_color = Color::Black;
    } else {
        text_color = Color::White;
    }
    Rect r(rect.x(), rect.y() + rect.height() / 2, rect.width(), rect.height() / 2);
    painter.draw_text(r, text, TextAlignment::Center, text_color);
}

void PianoWidget::render_piano(GPainter& painter)
{
    for (auto& kd : key_definitions)
        render_piano_key(painter, kd.index, kd.piano_key, kd.label);
}

static int knob_width = 100;

void PianoWidget::render_knob(GPainter& painter, const Rect& rect, bool state, const StringView& text)
{
    Color text_color;
    if (state) {
        painter.fill_rect(rect, Color(0, 200, 0));
        text_color = Color::Black;
    } else {
        painter.draw_rect(rect, Color(180, 0, 0));
        text_color = Color(180, 0, 0);
    }
    painter.draw_text(rect, text, TextAlignment::Center, text_color);
}

void PianoWidget::render_knobs(GPainter& painter)
{
    Rect delay_knob_rect(m_width - knob_width - 16, m_height - 50, knob_width, 16);
    render_knob(painter, delay_knob_rect, m_delay_enabled, "V: Delay   ");

    Rect release_knob_rect(m_width - knob_width - 16, m_height - 30, knob_width, 16);
    render_knob(painter, release_knob_rect, m_release_enabled, "B: Release ");

    Rect octave_knob_rect(m_width - knob_width - 16 - knob_width - 16, m_height - 50, knob_width, 16);
    auto text = String::format("Z/X: Oct %d ", m_octave);
    int oct_rgb_step = 255 / (m_octave_max + 4);
    int oshade = (m_octave + 4) * oct_rgb_step;
    painter.draw_rect(octave_knob_rect, Color(oshade, oshade, oshade));
    painter.draw_text(octave_knob_rect, text, TextAlignment::Center, Color(oshade, oshade, oshade));

    int r = 0, g = 0, b = 0;
    if (m_wave_type == WaveType::Sine) {
        r = 255;
        g = 192;
        b = 0;
    } else if (m_wave_type == WaveType::Saw) {
        r = 240;
        g = 100;
        b = 128;
    } else if (m_wave_type == WaveType::Square) {
        r = 128;
        g = 160;
        b = 255;
    }
    Rect wave_knob_rect(m_width - knob_width - 16 - knob_width - 16, m_height - 30, knob_width, 16);
    const char* wave_name = "";
    if (m_wave_type == WaveType::Sine)
        wave_name = "C: Sine    ";
    else if (m_wave_type == WaveType::Saw)
        wave_name = "C: Sawtooth";
    else if (m_wave_type == WaveType::Square)
        wave_name = "C: Square  ";
    painter.draw_rect(wave_knob_rect, Color(r, g, b));
    painter.draw_text(wave_knob_rect, wave_name, TextAlignment::Center, Color(r, g, b));
}

void PianoWidget::custom_event(CCustomEvent&)
{
    update();
}
