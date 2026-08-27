"""Revisioned semantic state shared by every host adapter and HID output."""

from __future__ import annotations

from dataclasses import dataclass

from .protocol import (
    DEFAULT_CIVIC,
    EMPTY_SUMMARY,
    CivicState,
    Floor,
    Intensity,
    Mode,
    NotificationSummary,
    Scene,
    Secondary,
)


@dataclass(frozen=True, slots=True)
class SemanticState:
    scene: Scene = Scene.DUEL
    summary: NotificationSummary = EMPTY_SUMMARY
    civic: CivicState = DEFAULT_CIVIC
    revision: int = 0


class SemanticResolver:
    """Resolve adapter inputs with one explicit scene-precedence policy.

    Alongside the earlier ``scene``/``summary`` the resolver now derives the Twin Cities
    civic bytes. Only integer enums ever leave this object: no window
    title, URL, path, repo, or notification text influences anything but which
    enum is selected, so the ``CivicState`` handed to ``build_packet`` is a pure
    enum tuple by construction.
    """

    def __init__(self, override: Scene | None = None) -> None:
        self.override = override
        self.focus_scene = Scene.DUEL
        self.focus_floor = Floor.COMMONS
        self.focus_matched = False
        self.dnd = False
        self.pomodoro = False
        self.pomodoro_stage = Intensity.CALM
        self.media_playing = False
        self.intensity = Intensity.CALM
        self.transfer_active = False
        self.system_alert = False
        self.browser_activity = Secondary.NONE
        self.browser_intensity = Intensity.CALM
        self.state = SemanticState()

    def _resolve_secondary(self) -> Secondary:
        # One bounded supporting channel with fixed, documented precedence.
        if self.system_alert:
            return Secondary.SYSTEM
        if self.transfer_active:
            return Secondary.TRANSFER
        if self.pomodoro:
            return Secondary.CALENDAR
        if self.browser_activity == Secondary.PAGE:
            return Secondary.PAGE
        if self.browser_activity == Secondary.TAB:
            return Secondary.TAB
        if self.browser_activity == Secondary.SCROLL:
            return Secondary.SCROLL
        if self.media_playing:
            return Secondary.MEDIA
        return Secondary.NONE

    def update(
        self,
        summary: NotificationSummary | None = None,
        *,
        focus_scene: Scene | None = None,
        focus_floor: Floor | None = None,
        focus_matched: bool | None = None,
        dnd: bool | None = None,
        pomodoro: bool | None = None,
        pomodoro_stage: Intensity | None = None,
        media_playing: bool | None = None,
        intensity: Intensity | None = None,
        transfer_active: bool | None = None,
        system_alert: bool | None = None,
        browser_activity: Secondary | None = None,
        browser_intensity: Intensity | None = None,
    ) -> bool:
        if focus_scene is not None:
            self.focus_scene = focus_scene
        if focus_floor is not None:
            self.focus_floor = focus_floor
        if focus_matched is not None:
            self.focus_matched = focus_matched
        if dnd is not None:
            self.dnd = dnd
        if pomodoro is not None:
            self.pomodoro = pomodoro
        if pomodoro_stage is not None:
            self.pomodoro_stage = pomodoro_stage
        if media_playing is not None:
            self.media_playing = media_playing
        if intensity is not None:
            self.intensity = intensity
        if transfer_active is not None:
            self.transfer_active = transfer_active
        if system_alert is not None:
            self.system_alert = system_alert
        if browser_activity is not None:
            self.browser_activity = browser_activity
        if browser_intensity is not None:
            self.browser_intensity = browser_intensity
        current_summary = self.state.summary if summary is None else summary
        # Media is a fallback, not an override. It fills in ARCHIVE only when no
        # profile claimed the focused window, so playing something in the
        # background can neither hide a recognised application nor forge a
        # scene/floor pair the firmware reads as a different district -- coding
        # with music stays the Workshop rather than becoming the Undercroft.
        # The Pomodoro ritual still outranks everything but an explicit override.
        scene = (
            self.override
            if self.override is not None
            else Scene.FOCUS
            if self.pomodoro
            else Scene.ARCHIVE
            if self.media_playing and not self.focus_matched
            else self.focus_scene
        )
        mode = Mode.QUIET if (self.dnd or self.pomodoro) else Mode.NORMAL
        intensity = (
            self.pomodoro_stage
            if self.pomodoro
            else self.browser_intensity
            if self.browser_activity != Secondary.NONE
            else self.intensity
        )
        civic = CivicState(
            floor=Floor.SPECIAL if self.pomodoro else self.focus_floor,
            mode=mode,
            intensity=intensity,
            secondary=self._resolve_secondary(),
        )
        if (
            scene == self.state.scene
            and current_summary == self.state.summary
            and civic == self.state.civic
        ):
            return False
        self.state = SemanticState(scene, current_summary, civic, self.state.revision + 1)
        return True
