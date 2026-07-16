# Corne Arcane earlier command-completion and repository-state hook for Zsh.
# Only monotonic duration and integer exit status leave the shell. Command
# text, paths, environment, and terminal content are never transmitted.

typeset -gF _corne_arcane_started=0
typeset -g _corne_arcane_git_state=""

_corne_arcane_uptime() {
  local _rest
  IFS=' ' read -r REPLY _rest < /proc/uptime
}

_corne_arcane_preexec() {
  _corne_arcane_uptime
  _corne_arcane_started=$REPLY
}

_corne_arcane_precmd() {
  local _status=$? _now _elapsed_ms _state
  if (( _corne_arcane_started > 0 )); then
    _corne_arcane_uptime
    _now=$REPLY
    (( _elapsed_ms = (_now - _corne_arcane_started) * 1000 ))
    _corne_arcane_started=0
    if (( _elapsed_ms >= 10000 )); then
      command corne-arcane-event terminal $_elapsed_ms $_status >/dev/null 2>&1 &!
    fi
  fi
  if command git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if command git rev-parse -q --verify MERGE_HEAD >/dev/null 2>&1 ||
       command git rebase --show-current-patch >/dev/null 2>&1; then
      _state=operation
    elif command git diff --quiet --ignore-submodules -- 2>/dev/null &&
         command git diff --cached --quiet --ignore-submodules -- 2>/dev/null; then
      _state=clean
    else
      _state=dirty
    fi
    if [[ $_corne_arcane_git_state == operation && $_state != operation ]]; then
      if (( _status == 0 )); then
        command corne-arcane-event git completion >/dev/null 2>&1 &!
      else
        command corne-arcane-event git completion --failed >/dev/null 2>&1 &!
      fi
    fi
    if [[ $_state != $_corne_arcane_git_state ]]; then
      _corne_arcane_git_state=$_state
      command corne-arcane-event git $_state >/dev/null 2>&1 &!
    fi
  else
    _corne_arcane_git_state=""
  fi
}

autoload -Uz add-zsh-hook
add-zsh-hook preexec _corne_arcane_preexec
add-zsh-hook precmd _corne_arcane_precmd
