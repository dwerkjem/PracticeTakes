#!/usr/bin/env zsh

setopt err_exit no_unset pipe_fail
zmodload zsh/datetime
zmodload zsh/zselect

readonly script_dir=${0:A:h}
readonly repository_root=${script_dir:h:h:h}
readonly validation_dir=$repository_root/build/ui-validation
readonly app=$repository_root/build/bin/PracticeTakes
readonly capture=$validation_dir/xwindow_capture
readonly pointer=$validation_dir/pointer_control
readonly window_control=$validation_dir/window_control
readonly title_bar_height=42

mkdir -p "$validation_dir"
typeset -a x11_flags xtst_flags
x11_flags=(${(z)"$(pkg-config --cflags --libs x11)"})
xtst_flags=(${(z)"$(pkg-config --cflags --libs x11 xtst)"})
"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    "$script_dir/xwindow_capture.cpp" -o "$capture" "${x11_flags[@]}"
"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    "$script_dir/pointer_control.cpp" -o "$pointer" "${xtst_flags[@]}"
"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    "$script_dir/window_control.cpp" -o "$window_control" "${x11_flags[@]}"

if [[ ! -x "$app" ]]; then
    print -u2 -- "Build Practice Takes before running visual validation"
    exit 1
fi
if pgrep -x PracticeTakes >/dev/null 2>&1; then
    print -u2 -- "Close the running Practice Takes instance before visual validation"
    exit 1
fi

read -r original_x original_y < <("$pointer")
typeset golden_pid=""
typeset candidate_pid=""

park_pointer()
{
    "$pointer" "$park_x" "$park_y"
}

restore_pointer()
{
    "$pointer" "$original_x" "$original_y" >/dev/null 2>&1 || true
}

stop_app()
{
    local pid=$1
    if [[ -n "$pid" ]]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

cleanup()
{
    restore_pointer
    stop_app "$golden_pid"
    stop_app "$candidate_pid"
}
trap cleanup EXIT INT TERM
read -r park_x park_y < <("$pointer" --park)

rm -f "$validation_dir"/cursor-{golden,candidate,maximized,restored}.{ppm,png,log}
rm -f "$validation_dir"/cursor-{candidate,maximized,restored}.tmp.ppm
rm -f "$validation_dir"/cursor-{golden,restored}-content{,.tmp}.ppm
rm -f "$validation_dir"/cursor-{candidate.pid,timing.txt,responsiveness.txt}

park_pointer
typeset -F 6 golden_started=$EPOCHREALTIME
"$app" --mute-microphone > "$validation_dir/cursor-golden.log" 2>&1 &
golden_pid=$!

while (( EPOCHREALTIME - golden_started < 7.0 )); do
    park_pointer
    zselect -t 10 || true
done

park_pointer
"$capture" "$golden_pid" "$validation_dir/cursor-golden.ppm"
typeset -F 6 golden_captured=$EPOCHREALTIME
ffmpeg -hide_banner -loglevel error \
    -i "$validation_dir/cursor-golden.ppm" -frames:v 1 -y \
    "$validation_dir/cursor-golden.png"
ffmpeg -hide_banner -loglevel error \
    -i "$validation_dir/cursor-golden.ppm" \
    -vf "crop=iw:ih-${title_bar_height}:0:${title_bar_height}" -frames:v 1 -y \
    "$validation_dir/cursor-golden-content.ppm"
read -r golden_width golden_height < <(sed -n '2p' "$validation_dir/cursor-golden.ppm")
stop_app "$golden_pid"
golden_pid=""

park_pointer
typeset -F 6 candidate_started=$EPOCHREALTIME
"$app" --mute-microphone > "$validation_dir/cursor-candidate.log" 2>&1 &
candidate_pid=$!
typeset -F 6 first_frame=0
typeset -F 6 matched=0
integer attempts=0

while (( EPOCHREALTIME - candidate_started < 10.0 )); do
    (( ++attempts ))
    park_pointer
    if "$capture" "$candidate_pid" "$validation_dir/cursor-candidate.tmp.ppm" 2>/dev/null; then
        typeset -F 6 captured=$EPOCHREALTIME
        if (( first_frame == 0 )); then
            first_frame=$captured
        fi
        if cmp -s \
            "$validation_dir/cursor-golden.ppm" \
            "$validation_dir/cursor-candidate.tmp.ppm"; then
            matched=$captured
            mv \
                "$validation_dir/cursor-candidate.tmp.ppm" \
                "$validation_dir/cursor-candidate.ppm"
            break
        fi
    fi
done

if (( matched == 0 )); then
    print -u2 -- "No cursor-controlled golden match within 10 seconds"
    exit 1
fi

ffmpeg -hide_banner -loglevel error \
    -i "$validation_dir/cursor-candidate.ppm" -frames:v 1 -y \
    "$validation_dir/cursor-candidate.png"
read -r actual_x actual_y < <("$pointer")
if [[ "$actual_x" != "$park_x" || "$actual_y" != "$park_y" ]]; then
    print -u2 -- "Pointer moved during visual validation"
    exit 1
fi

typeset -F 6 maximize_started=$EPOCHREALTIME
"$window_control" "$candidate_pid" maximize
typeset -F 6 maximized=0
while (( EPOCHREALTIME - maximize_started < 5.0 )); do
    park_pointer
    if "$capture" "$candidate_pid" "$validation_dir/cursor-maximized.tmp.ppm" 2>/dev/null; then
        read -r current_width current_height \
            < <(sed -n '2p' "$validation_dir/cursor-maximized.tmp.ppm")
        if [[ "$current_width" != "$golden_width" || "$current_height" != "$golden_height" ]]; then
            maximized_width=$current_width
            maximized_height=$current_height
            maximized=$EPOCHREALTIME
            mv \
                "$validation_dir/cursor-maximized.tmp.ppm" \
                "$validation_dir/cursor-maximized.ppm"
            break
        fi
    fi
done
if (( maximized == 0 )); then
    print -u2 -- "The window did not repaint at maximized dimensions"
    exit 1
fi
ffmpeg -hide_banner -loglevel error \
    -i "$validation_dir/cursor-maximized.ppm" -frames:v 1 -y \
    "$validation_dir/cursor-maximized.png"

typeset -F 6 restore_started=$EPOCHREALTIME
"$window_control" "$candidate_pid" restore
typeset -F 6 restored=0
while (( EPOCHREALTIME - restore_started < 5.0 )); do
    park_pointer
    if "$capture" "$candidate_pid" "$validation_dir/cursor-restored.tmp.ppm" 2>/dev/null; then
        read -r current_width current_height \
            < <(sed -n '2p' "$validation_dir/cursor-restored.tmp.ppm")
        if [[ "$current_width" == "$golden_width" && "$current_height" == "$golden_height" ]]; then
            ffmpeg -hide_banner -loglevel error \
                -i "$validation_dir/cursor-restored.tmp.ppm" \
                -vf "crop=iw:ih-${title_bar_height}:0:${title_bar_height}" -frames:v 1 -y \
                "$validation_dir/cursor-restored-content.tmp.ppm"
            if cmp -s \
                "$validation_dir/cursor-golden-content.ppm" \
                "$validation_dir/cursor-restored-content.tmp.ppm"; then
                restored=$EPOCHREALTIME
                mv \
                    "$validation_dir/cursor-restored.tmp.ppm" \
                    "$validation_dir/cursor-restored.ppm"
                mv \
                    "$validation_dir/cursor-restored-content.tmp.ppm" \
                    "$validation_dir/cursor-restored-content.ppm"
                break
            fi
        fi
    fi
done
if (( restored == 0 )); then
    print -u2 -- "The restored application content did not match the golden"
    exit 1
fi
ffmpeg -hide_banner -loglevel error \
    -i "$validation_dir/cursor-restored.ppm" -frames:v 1 -y \
    "$validation_dir/cursor-restored.png"

printf \
    'GOLDEN_CAPTURE_MS=%.3f\nAPP_PID=%s\nATTEMPTS=%s\nFIRST_RENDERED_FRAME_MS=%.3f\nUSABLE_GOLDEN_MATCH_MS=%.3f\nPIXEL_MATCH=exact\nCURSOR_PARKED=%s,%s\n' \
    "$(( (golden_captured - golden_started) * 1000.0 ))" \
    "$candidate_pid" \
    "$attempts" \
    "$(( (first_frame - candidate_started) * 1000.0 ))" \
    "$(( (matched - candidate_started) * 1000.0 ))" \
    "$park_x" \
    "$park_y" | tee "$validation_dir/cursor-timing.txt"
printf \
    'MAXIMIZE_REPAINT_MS=%.3f\nMAXIMIZED_SIZE=%sx%s\nRESTORE_CONTENT_MATCH_MS=%.3f\nRESTORED_CONTENT_MATCH=exact\nTITLE_BAR_FOCUS_EXCLUDED_PX=%s\nCURSOR_EXCLUDED=true\n' \
    "$(( (maximized - maximize_started) * 1000.0 ))" \
    "$maximized_width" \
    "$maximized_height" \
    "$(( (restored - restore_started) * 1000.0 ))" \
    "$title_bar_height" | tee "$validation_dir/cursor-responsiveness.txt"

stop_app "$candidate_pid"
candidate_pid=""