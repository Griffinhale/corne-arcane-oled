"""Revisioned semantic state shared by every host adapter and HID output."""

from __future__ import annotations

from dataclasses import dataclass

from .protocol import EMPTY_SUMMARY, NotificationSummary, Scene


@dataclass(frozen=True, slots=True)
class SemanticState:
    scene: Scene = Scene.DUEL
    summary: NotificationSummary = EMPTY_SUMMARY
    revision: int = 0


class SemanticResolver:
    """Resolve adapter inputs with one explicit scene-precedence policy."""

    def __init__(self, override: Scene | None = None) -> None:
        self.override = override
        self.focus_scene = Scene.DUEL
        self.dnd = False
        self.pomodoro = False
        self.media_playing = False
        self.state = SemanticState()

    def update(
        self,
        summary: NotificationSummary | None = None,
        *,
        focus_scene: Scene | None = None,
        dnd: bool | None = None,
        pomodoro: bool | None = None,
        media_playing: bool | None = None,
    ) -> bool:
        if focus_scene is not None:
            self.focus_scene = focus_scene
        if dnd is not None:
            self.dnd = dnd
        if pomodoro is not None:
            self.pomodoro = pomodoro
        if media_playing is not None:
            self.media_playing = media_playing
        current_summary = self.state.summary if summary is None else summary
        scene = (
            self.override
            if self.override is not None
            else Scene.FOCUS
            if self.dnd or self.pomodoro
            else Scene.ARCHIVE
            if self.media_playing
            else self.focus_scene
        )
        if scene == self.state.scene and current_summary == self.state.summary:
            return False
        self.state = SemanticState(scene, current_summary, self.state.revision + 1)
        return True
