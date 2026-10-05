# MIT license; Copyright (c) 2021 Amir Gonnen
# Copyright (c) 2024 - 2025 Kevin G. Schlosser


import lvgl as lv  # NOQA
import micropython  # NOQA
import sys
import time

from machine import Timer  # NOQA


TASK_HANDLER_STARTED = 0x01
TASK_HANDLER_FINISHED = 0x02

_default_timer_id = 0

if sys.platform in ('pyboard', 'rp2'):
    _default_timer_id = -1


class _DefaultUserData(object):
    pass


def _default_exception_hook(e):
    sys.print_exception(e)  # NOQA
    TaskHandler._current_instance.deinit()  # NOQA


class TaskHandler(object):
    _current_instance = None

    def __init__(
        self,
        duration=33,
        timer_id=_default_timer_id,
        max_scheduled=2,
        exception_hook=_default_exception_hook
    ):
        if TaskHandler._current_instance is not None:
            self.__dict__.update(TaskHandler._current_instance.__dict__)
        else:
            if not lv.is_initialized():
                lv.init()

            TaskHandler._current_instance = self

            self._callbacks = []

            self.duration = duration
            self.exception_hook = exception_hook

            self._timer = Timer(timer_id)

            # Allocation occurs here
            self._task_handler_ref = self._task_handler
            self.max_scheduled = max_scheduled

            self._last_tick = time.ticks_ms()  # NOQA
            # Fairness: how long the last handler pass took and when it ended,
            # so the timer can leave the main thread a share of the CPU.
            self._last_run_ms = 0
            self._last_end = self._last_tick
            self._timer.init(
                mode=Timer.PERIODIC,
                period=self.duration,
                callback=self._timer_cb
            )
            self._scheduled = 0
            self._running = False

    def add_event_cb(self, callback, event, user_data=_DefaultUserData):
        for i, (cb, evt, data) in enumerate(self._callbacks):
            if cb == callback:
                evt = event
                if user_data != _DefaultUserData:
                    data = user_data

                self._callbacks[i] = (cb, evt, data)
                break
        else:
            if user_data == _DefaultUserData:
                user_data = None

            self._callbacks.append((callback, event, user_data))

    def remove_event_cb(self, callback):
        for (cb, evt, data) in self._callbacks:
            if cb == callback:
                self._callbacks.remove((cb,evt,data))
                break

    def deinit(self):
        self._timer.deinit()
        TaskHandler._current_instance = None

    def disable(self):
        self._scheduled += self.max_scheduled

    def enable(self):
        self._scheduled -= self.max_scheduled

    @classmethod
    def is_running(cls):
        return cls._current_instance is not None

    def _task_handler(self, _):
        run_start = time.ticks_ms()  # NOQA
        try:
            self._scheduled -= 1

            if lv._nesting.value == 0:  # NOQA
                self._running = True

                run_update = True
                for cb, evt, data in self._callbacks:
                    if not evt & TASK_HANDLER_STARTED:
                        continue

                    try:
                        if cb(TASK_HANDLER_STARTED, data) is False:
                            run_update = False

                    except Exception as err:  # NOQA
                        if (
                            self.exception_hook and
                            self.exception_hook != _default_exception_hook
                        ):
                            self.exception_hook(err)
                        else:
                            sys.print_exception(err)  # NOQA
                        print(f"TaskHandler callback {cb} threw exception, disabling it")
                        self.remove_event_cb(cb)

                if run_update:
                    try:
                        lv.task_handler()
                    except Exception as e:
                        print(f"lv.task_handler() threw exception: {e}")
                        sys.print_exception(e)
                        # LVGL UI still hangs

                    for cb, evt, data in self._callbacks:
                        if not evt & TASK_HANDLER_FINISHED:
                            continue

                        try:
                            cb(TASK_HANDLER_FINISHED, data)
                        except Exception as err:  # NOQA
                            if (
                                self.exception_hook and
                                self.exception_hook != _default_exception_hook
                            ):
                                self.exception_hook(err)
                            else:
                                sys.print_exception(err)  # NOQA

                self._running = False

        except Exception as e:
            self._running = False
            
            if self.exception_hook:
                self.exception_hook(e)

        finally:
            end = time.ticks_ms()  # NOQA
            self._last_run_ms = time.ticks_diff(end, run_start)  # NOQA
            self._last_end = end

    def _timer_cb(self, _):
        # The only place LVGL time advances. Feed it the real elapsed time
        # rather than the nominal period: timer callbacks are delivered via the
        # scheduler and coalesce or drop under load, and _task_handler must not
        # add ticks of its own or LVGL time runs ~2x faster than wall clock.
        now = time.ticks_ms()  # NOQA
        lv.tick_inc(time.ticks_diff(now, self._last_tick))  # NOQA
        self._last_tick = now
        if self._running:
            return

        # Fairness: a handler pass that outlasts the timer period would
        # otherwise be rescheduled back-to-back, starving everything else on
        # the main thread (the REPL, touch and app tasks). Wait until at
        # least a quarter of the last pass's duration has elapsed since it
        # ended, so the main thread always keeps ~20% of the CPU.
        gap = self._last_run_ms >> 2
        if gap > self.duration and time.ticks_diff(now, self._last_end) < gap:  # NOQA
            return

        if self._scheduled < self.max_scheduled:
            try:
                micropython.schedule(self._task_handler_ref, 0)
                self._scheduled += 1
            except:  # NOQA
                pass
