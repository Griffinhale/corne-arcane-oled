# Privacy-bounded command completion and repository-state hook for Fish.
set -g __corne_arcane_started_ms 0
set -g __corne_arcane_git_state ''
set -g __corne_arcane_running_pid 0
set -q __corne_arcane_running_after; or set -g __corne_arcane_running_after 10

function __corne_arcane_uptime_ms
    read -l uptime _rest < /proc/uptime
    set -l parts (string split . -- $uptime)
    set -l fraction "$parts[2]000"
    math "$parts[1] * 1000 + "(string sub -l 3 -- $fraction)
end

function __corne_arcane_preexec --on-event fish_preexec
    set -g __corne_arcane_started_ms (__corne_arcane_uptime_ms)
    # Armed per command and disarmed at the next prompt, so only a command that
    # outlives the delay reports that it is running.
    command sh -c "sleep $__corne_arcane_running_after; exec corne-arcane-event terminal-start" \
        >/dev/null 2>&1 &
    set -g __corne_arcane_running_pid $last_pid
    disown $last_pid 2>/dev/null
end

function __corne_arcane_postexec --on-event fish_postexec
    set -l command_status $status
    if test $__corne_arcane_running_pid -gt 0
        command kill $__corne_arcane_running_pid 2>/dev/null
        set -g __corne_arcane_running_pid 0
    end
    if test $__corne_arcane_started_ms -gt 0
        set -l elapsed (math (__corne_arcane_uptime_ms) - $__corne_arcane_started_ms)
        set -g __corne_arcane_started_ms 0
        if test $elapsed -ge 10000
            command corne-arcane-event terminal $elapsed $command_status >/dev/null 2>&1 &
        end
    end
    set -l state ''
    if command git rev-parse --is-inside-work-tree >/dev/null 2>&1
        if command git rev-parse -q --verify MERGE_HEAD >/dev/null 2>&1; or \
           command git rebase --show-current-patch >/dev/null 2>&1
            set state operation
        else if command git diff --quiet --ignore-submodules -- 2>/dev/null; and \
                command git diff --cached --quiet --ignore-submodules -- 2>/dev/null; and \
                test -z (command git status --porcelain --untracked-files=normal 2>/dev/null)
            set state clean
        else
            set state dirty
        end
        if test "$__corne_arcane_git_state" = operation; and test "$state" != operation
            if test $command_status -eq 0
                command corne-arcane-event git completion >/dev/null 2>&1 &
            else
                command corne-arcane-event git completion --failed >/dev/null 2>&1 &
            end
        end
        if test "$state" != "$__corne_arcane_git_state"
            set -g __corne_arcane_git_state $state
            command corne-arcane-event git $state >/dev/null 2>&1 &
        end
    else
        set -g __corne_arcane_git_state ''
    end
end
