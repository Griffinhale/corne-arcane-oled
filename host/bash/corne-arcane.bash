# Privacy-bounded command completion and repository-state hook for Bash.
# Only monotonic duration, integer exit status, a normalized repository enum,
# and one bare "still running" call for a command past ten seconds leave this
# shell. Existing PROMPT_COMMAND entries and DEBUG traps run.

# Re-sourcing must not stack DEBUG/PROMPT handlers (common with shell config
# reloaders). `return` is valid because this file is an opt-in sourced hook.
if [[ ${_corne_arcane_hook_loaded:-0} == 1 ]]; then
  return 0
fi
_corne_arcane_hook_loaded=1

_corne_arcane_started_ms=0
_corne_arcane_git_state=
_corne_arcane_silent=x
_corne_arcane_running_pid=0
_corne_arcane_running_after=${_corne_arcane_running_after:-10}

_corne_arcane_uptime_ms() {
  local _seconds _fraction _rest
  IFS='. ' read -r _seconds _fraction _rest < /proc/uptime
  _fraction=${_fraction}000
  printf '%u' "$((10#${_seconds} * 1000 + 10#${_fraction:0:3}))"
}

_corne_arcane_preexec() {
  _corne_arcane_uptime_ms
}

# Armed per command and disarmed at the next prompt, so only a command that
# outlives the delay reports that it is running. Its output goes nowhere, so
# the command substitution in PS0 returns at once with the pid.
_corne_arcane_arm() {
  command sh -c "sleep $_corne_arcane_running_after; exec corne-arcane-event terminal-start" \
    >/dev/null 2>&1 &
  printf '%u' "$!"
}

_corne_arcane_precmd() {
  local _status=$? _now _elapsed _state
  if (( _corne_arcane_running_pid > 0 )); then
    command kill "$_corne_arcane_running_pid" 2>/dev/null
    _corne_arcane_running_pid=0
  fi
  if (( _corne_arcane_started_ms > 0 )); then
    _now=$(_corne_arcane_uptime_ms)
    _elapsed=$((_now - _corne_arcane_started_ms))
    _corne_arcane_started_ms=0
    if (( _elapsed >= 10000 )); then
      command corne-arcane-event terminal "$_elapsed" "$_status" >/dev/null 2>&1 &
    fi
  fi
  if command git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if command git rev-parse -q --verify MERGE_HEAD >/dev/null 2>&1 ||
       command git rebase --show-current-patch >/dev/null 2>&1; then
      _state=operation
    elif command git diff --quiet --ignore-submodules -- 2>/dev/null &&
         command git diff --cached --quiet --ignore-submodules -- 2>/dev/null &&
         [[ -z $(command git status --porcelain --untracked-files=normal 2>/dev/null) ]]; then
      _state=clean
    else
      _state=dirty
    fi
    if [[ $_corne_arcane_git_state == operation && $_state != operation ]]; then
      if (( _status == 0 )); then
        command corne-arcane-event git completion >/dev/null 2>&1 &
      else
        command corne-arcane-event git completion --failed >/dev/null 2>&1 &
      fi
    fi
    if [[ $_state != "$_corne_arcane_git_state" ]]; then
      _corne_arcane_git_state=$_state
      command corne-arcane-event git "$_state" >/dev/null 2>&1 &
    fi
  else
    _corne_arcane_git_state=
  fi
  # PROMPT_COMMAND entries after this hook still observe the user's status.
  return "$_status"
}

# PS0 expands after Bash reads a command and immediately before it executes.
# Command substitution obtains the monotonic value; the arithmetic assignment
# itself is evaluated by the interactive shell. A zero-length parameter slice
# keeps the marker invisible. Existing PS0 and the caller's DEBUG trap remain
# untouched.
PS0='${_corne_arcane_silent:0:$((_corne_arcane_started_ms=$(_corne_arcane_preexec),_corne_arcane_running_pid=$(_corne_arcane_arm),0))}'"${PS0-}"

if declare -p PROMPT_COMMAND 2>/dev/null | command grep -q 'declare -a'; then
  PROMPT_COMMAND=(_corne_arcane_precmd "${PROMPT_COMMAND[@]}")
else
  PROMPT_COMMAND="_corne_arcane_precmd${PROMPT_COMMAND:+;$PROMPT_COMMAND}"
fi
