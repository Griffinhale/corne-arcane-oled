# Corne Arcane M10 command-completion hook for Zsh.
# Only monotonic duration and integer exit status leave the shell. Command
# text, working directory, environment, and terminal content are never read.

typeset -gF _corne_arcane_started=0

_corne_arcane_uptime() {
  local _rest
  IFS=' ' read -r REPLY _rest < /proc/uptime
}

_corne_arcane_preexec() {
  _corne_arcane_uptime
  _corne_arcane_started=$REPLY
}

_corne_arcane_precmd() {
  local _status=$? _now _elapsed_ms
  (( _corne_arcane_started > 0 )) || return
  _corne_arcane_uptime
  _now=$REPLY
  (( _elapsed_ms = (_now - _corne_arcane_started) * 1000 ))
  _corne_arcane_started=0
  (( _elapsed_ms >= 10000 )) || return
  command corne-arcane-event terminal $_elapsed_ms $_status >/dev/null 2>&1 &!
}

autoload -Uz add-zsh-hook
add-zsh-hook preexec _corne_arcane_preexec
add-zsh-hook precmd _corne_arcane_precmd
