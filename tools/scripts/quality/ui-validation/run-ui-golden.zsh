#!/usr/bin/env zsh

setopt err_exit no_unset pipe_fail
zmodload zsh/datetime
zmodload zsh/zselect

readonly script_dir=${0:A:h}
readonly repository_root=${script_dir:h:h:h:h}
readonly validation_root=$repository_root/build/ui-validation
readonly evidence_dir=$validation_root/step-7
readonly profiles_dir=$evidence_dir/profiles
readonly golden_dir=$evidence_dir/goldens
readonly launch_dir=$evidence_dir/launch-validation
readonly workflow_dir=$evidence_dir/workflows
readonly manifest=$evidence_dir/manifest.txt
readonly app=$repository_root/build/bin/PracticeTakes
readonly capture=$validation_root/xwindow_capture
readonly pointer=$validation_root/pointer_control
readonly window_control=$validation_root/window_control
readonly cxx=/usr/bin/g++
readonly title_bar_height=42
readonly settle_seconds=7
readonly baseline_ms=414.134
readonly constrained_width=800
readonly constrained_height=600
# Settings moved under the XDG config directory in 5ab9b85; the app resolves
# this from $HOME, which every profile below overrides.
readonly settings_relative_path=.config/PracticeTakes/PracticeTakes.settings

if [[ ! -x "$app" ]]; then
    print -u2 -- "Build Practice Takes before running visual validation"
    exit 1
fi
if pgrep -x PracticeTakes >/dev/null 2>&1; then
    print -u2 -- "Close the running Practice Takes instance before visual validation"
    exit 1
fi

rm -rf "$evidence_dir"
mkdir -p "$profiles_dir" "$golden_dir" "$launch_dir" "$workflow_dir"

typeset -a x11_flags xtst_flags
x11_flags=(${(z)"$(pkg-config --cflags --libs x11)"})
xtst_flags=(${(z)"$(pkg-config --cflags --libs x11 xtst)"})
"$cxx" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    "$script_dir/xwindow_capture.cpp" -o "$capture" "${x11_flags[@]}"
"$cxx" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    "$script_dir/pointer_control.cpp" -o "$pointer" "${xtst_flags[@]}"
"$cxx" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    "$script_dir/window_control.cpp" -o "$window_control" "${x11_flags[@]}"

read -r original_x original_y < <("$pointer")
read -r park_x park_y < <("$pointer" --park)
typeset active_pid=""

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
    if [[ -n "$active_pid" ]]; then
        kill "$active_pid" 2>/dev/null || true
        wait "$active_pid" 2>/dev/null || true
        active_pid=""
    fi
}

cleanup()
{
    restore_pointer
    stop_app
}
trap cleanup EXIT INT TERM

wait_parked()
{
    local -F duration=$1
    local -F started=$EPOCHREALTIME
    while (( EPOCHREALTIME - started < duration )); do
        park_pointer
        zselect -t 10 || true
    done
}

reset_home()
{
    local target=$1
    local seed=${2:-}
    rm -rf "$target"
    mkdir -p "$target"
    if [[ -n "$seed" ]]; then
        cp -a "$seed/." "$target/"
    fi
}

start_app()
{
    local home=$1
    local log=$2
    shift 2
    HOME="$home" "$app" --mute-microphone "$@" > "$log" 2>&1 &
    active_pid=$!
}

capture_with_retry()
{
    local pid=$1
    local output=$2
    shift 2
    local -F started=$EPOCHREALTIME
    while (( EPOCHREALTIME - started < 5.0 )); do
        if "$capture" "$pid" "$output" "$@" 2>/dev/null; then
            return 0
        fi
        zselect -t 5 || true
    done
    print -u2 -- "No process-owned window was capturable for PID $pid"
    return 1
}

to_png()
{
    ffmpeg -hide_banner -loglevel error -i "$1" -frames:v 1 -y "$2"
}

record_dimensions()
{
    sed -n '2p' "$1"
}

prepare_restored_seed()
{
    local home=$profiles_dir/restored-seed
    reset_home "$home"
    start_app "$home" "$evidence_dir/prepare-restored.log" \
        --ui-validation-scenario prepare-restored
    wait_parked 2.0
    stop_app
    if [[ ! -f "$home/$settings_relative_path" ]]; then
        print -u2 -- "The representative restored profile was not persisted"
        return 1
    fi
}

generate_golden()
{
    local name=$1
    local seed=${2:-}
    local home=$profiles_dir/$name-golden
    reset_home "$home" "$seed"
    start_app "$home" "$golden_dir/$name.log"
    local -F started=$EPOCHREALTIME
    wait_parked "$settle_seconds"
    capture_with_retry \
        "$active_pid" "$golden_dir/$name.ppm" --crop-top "$title_bar_height"
    local -F captured=$EPOCHREALTIME
    to_png "$golden_dir/$name.ppm" "$golden_dir/$name.png"
    read -r width height < <(record_dimensions "$golden_dir/$name.ppm")
    printf \
        '%s_GOLDEN_SETTLE_MS=%.3f\n%s_GOLDEN_SIZE=%sx%s\n%s_GOLDEN_POINTER=%s,%s\n%s_GOLDEN_TITLE_BAR_EXCLUDED_PX=%s\n' \
        "${(U)name}" "$(( (captured - started) * 1000.0 ))" \
        "${(U)name}" "$width" "$height" \
        "${(U)name}" "$park_x" "$park_y" \
        "${(U)name}" "$title_bar_height" >> "$manifest"
    stop_app
}

verify_responsive_restore()
{
    local pid=$1
    local golden=$2
    local prefix=$3
    read -r original_width original_height < <(record_dimensions "$golden")

    local -F resize_started=$EPOCHREALTIME
    "$window_control" "$pid" maximize
    local -F resized=0
    while (( EPOCHREALTIME - resize_started < 5.0 )); do
        park_pointer
        if "$capture" "$pid" "$prefix-maximized.tmp.ppm" \
            --crop-top "$title_bar_height" 2>/dev/null; then
            read -r width height < <(record_dimensions "$prefix-maximized.tmp.ppm")
            if [[ "$width" != "$original_width" || "$height" != "$original_height" ]]; then
                resized=$EPOCHREALTIME
                mv "$prefix-maximized.tmp.ppm" "$prefix-maximized.ppm"
                break
            fi
        fi
    done
    if (( resized == 0 )); then
        print -u2 -- "The restored workspace did not repaint after maximize"
        return 1
    fi
    to_png "$prefix-maximized.ppm" "$prefix-maximized.png"

    local -F restore_started=$EPOCHREALTIME
    "$window_control" "$pid" restore
    local -F restored=0
    while (( EPOCHREALTIME - restore_started < 5.0 )); do
        park_pointer
        if "$capture" "$pid" "$prefix-restored.tmp.ppm" \
            --crop-top "$title_bar_height" 2>/dev/null && \
            cmp -s "$golden" "$prefix-restored.tmp.ppm"; then
            restored=$EPOCHREALTIME
            mv "$prefix-restored.tmp.ppm" "$prefix-restored.ppm"
            break
        fi
    done
    if (( restored == 0 )); then
        print -u2 -- "The restored workspace did not return to its exact golden pixels"
        return 1
    fi
    to_png "$prefix-restored.ppm" "$prefix-restored.png"
    printf \
        'RESPONSIVE_MAXIMIZE_MS=%.3f\nRESPONSIVE_RESTORE_MS=%.3f\nRESPONSIVE_RESTORE_MATCH=exact\n' \
        "$(( (resized - resize_started) * 1000.0 ))" \
        "$(( (restored - restore_started) * 1000.0 ))" >> "$manifest"
}

validate_launch()
{
    local name=$1
    local seed=${2:-}
    local responsive=${3:-false}
    local home=$profiles_dir/$name-candidate
    local golden=$golden_dir/$name.ppm
    local prefix=$launch_dir/$name
    reset_home "$home" "$seed"
    park_pointer
    local -F started=$EPOCHREALTIME
    start_app "$home" "$prefix.log"
    local -F first_frame=0
    local -F matched=0
    integer attempts=0

    while (( EPOCHREALTIME - started < 10.0 )); do
        (( ++attempts ))
        park_pointer
        if "$capture" "$active_pid" "$prefix.tmp.ppm" \
            --crop-top "$title_bar_height" 2>/dev/null; then
            local -F captured=$EPOCHREALTIME
            if (( first_frame == 0 )); then
                first_frame=$captured
            fi
            if cmp -s "$golden" "$prefix.tmp.ppm"; then
                matched=$captured
                mv "$prefix.tmp.ppm" "$prefix.ppm"
                break
            fi
        fi
    done
    if (( matched == 0 )); then
        print -u2 -- "$name did not exactly match its golden within 10 seconds"
        return 1
    fi
    to_png "$prefix.ppm" "$prefix.png"
    read -r actual_x actual_y < <("$pointer")
    if [[ "$actual_x" != "$park_x" || "$actual_y" != "$park_y" ]]; then
        print -u2 -- "Pointer moved during $name launch validation"
        return 1
    fi

    local -F match_ms=$(( (matched - started) * 1000.0 ))
    local -F delta_ms=$(( match_ms - baseline_ms ))
    local -F delta_percent=$(( delta_ms * 100.0 / baseline_ms ))
    printf \
        '%s_FIRST_FRAME_MS=%.3f\n%s_EXACT_MATCH_MS=%.3f\n%s_BASELINE_MS=%.3f\n%s_DELTA_MS=%+.3f\n%s_DELTA_PERCENT=%+.2f\n%s_ATTEMPTS=%s\n%s_PIXEL_MATCH=exact\n%s_VISIBLE_PROCESS_WINDOW=true\n' \
        "${(U)name}" "$(( (first_frame - started) * 1000.0 ))" \
        "${(U)name}" "$match_ms" \
        "${(U)name}" "$baseline_ms" \
        "${(U)name}" "$delta_ms" \
        "${(U)name}" "$delta_percent" \
        "${(U)name}" "$attempts" \
        "${(U)name}" \
        "${(U)name}" >> "$manifest"

    if [[ "$responsive" == true ]]; then
        verify_responsive_restore "$active_pid" "$golden" "$prefix"
    fi
    stop_app
}

verify_pointer_insensitive()
{
    local pid=$1
    local reference=$2
    local candidate=$3
    shift 3
    "$pointer" 0 0
    zselect -t 20 || true
    "$capture" "$pid" "$candidate" "$@"
    if ! cmp -s "$reference" "$candidate"; then
        print -u2 -- "Pointer position changed process-owned pixels for $reference"
        return 1
    fi
    park_pointer
    rm -f "$candidate"
}

capture_workflow()
{
    local scenario=$1
    local window_title=${2:-}
    local home=$profiles_dir/workflow-$scenario
    local output=$workflow_dir/$scenario
    reset_home "$home"
    start_app "$home" "$output.log" --ui-validation-scenario "$scenario"
    wait_parked 2.5

    typeset -a capture_options control_options
    capture_options=(--crop-top "$title_bar_height")
    control_options=()
    if [[ -n "$window_title" ]]; then
        capture_options+=(--title "$window_title")
        control_options=(--title "$window_title")
    fi

    capture_with_retry "$active_pid" "$output-desktop.ppm" "${capture_options[@]}"
    to_png "$output-desktop.ppm" "$output-desktop.png"
    verify_pointer_insensitive \
        "$active_pid" "$output-desktop.ppm" "$output-desktop-pointer.tmp.ppm" \
        "${capture_options[@]}"
    read -r desktop_width desktop_height < <(record_dimensions "$output-desktop.ppm")

    "$window_control" "$active_pid" resize "$constrained_width" "$constrained_height" \
        "${control_options[@]}"
    wait_parked 1.0
    capture_with_retry "$active_pid" "$output-constrained.ppm" "${capture_options[@]}"
    read -r constrained_actual_width constrained_actual_height \
        < <(record_dimensions "$output-constrained.ppm")
    if [[ "$constrained_actual_width" != "$constrained_width" || \
          "$constrained_actual_height" != "$(( constrained_height - title_bar_height ))" ]]; then
        print -u2 -- "$scenario did not reach the requested constrained content size"
        return 1
    fi
    to_png "$output-constrained.ppm" "$output-constrained.png"
    verify_pointer_insensitive \
        "$active_pid" "$output-constrained.ppm" "$output-constrained-pointer.tmp.ppm" \
        "${capture_options[@]}"

    printf \
        '%s_DESKTOP_SIZE=%sx%s\n%s_CONSTRAINED_SIZE=%sx%s\n%s_POINTER_COMPARISON=exact\n' \
        "${(U)scenario}" "$desktop_width" "$desktop_height" \
        "${(U)scenario}" "$constrained_actual_width" "$constrained_actual_height" \
        "${(U)scenario}" >> "$manifest"
    stop_app
}

cat > "$manifest" <<EOF
VALIDATION=workspace-layout-restoration-step-7
PROCESS_OWNED_CAPTURE=true
MICROPHONE_MUTED=true
SETTLE_SECONDS=$settle_seconds
TITLE_BAR_EXCLUDED_PX=$title_bar_height
POINTER_PARKED=$park_x,$park_y
USABLE_TIME_BASELINE_MS=$baseline_ms
EOF

prepare_restored_seed
generate_golden first-launch
generate_golden restored-workspace "$profiles_dir/restored-seed"
validate_launch first-launch
validate_launch restored-workspace "$profiles_dir/restored-seed" true

capture_workflow import-confirmation
capture_workflow import-validation-error
capture_workflow import-rollback-error
capture_workflow long-workspace-name
capture_workflow disconnected-display Spectrogram

print 'GOLDENS_APPROVED=first-launch,restored-workspace' >> "$manifest"
print 'WORKFLOWS_VALIDATED=import-confirmation,import-validation-error,import-rollback-error,long-workspace-name,disconnected-display' >> "$manifest"
print 'STEP_7_RESULT=pass' >> "$manifest"

cat "$manifest"
