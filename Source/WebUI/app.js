// Auric Z-CLIP WebUI — app.js
// FIX: os2x toggle ONLY on purple LED (.led). 2X is NOT a toggle.

(() => {
  const PARAMS = new Set(["pre","trim","satclip","mix","drive","ceiling","os2x"]);
  const DEFAULTS = { pre:0.35, trim:0.35, satclip:0.5, mix:1.0, drive:0.5, ceiling:0.7, os2x:0 };

  const clamp01 = (v) => Math.min(1, Math.max(0, v));
  const valueToDeg = (v) => -135 + v * 270;
  const quantizeOs2x = (v) => (v >= 0.5 ? 1 : 0);

  // Queue host calls if __setParam comes before DOM ready
  const __hostQueue = [];
  const __earlySetParam = (id, value) => { __hostQueue.push([id, value]); };
  if (typeof window.__setParam !== "function") window.__setParam = __earlySetParam;

  const postParam = (id, value) => {
    try {
      if (window.juce && typeof window.juce.postMessage === "function") {
        window.juce.postMessage(JSON.stringify({ type: "param", id, value }));
      }
    } catch {}
  };

  const boot = () => {
    // Initial UI state (host overrides via __setParam)
    const state = { pre:0.5, trim:0.5, satclip:0.5, mix:0.5, drive:0.5, ceiling:0.5, os2x:0 };

    const knobEls = {};
    document.querySelectorAll(".small-knob, .drive-knob").forEach((el) => {
      const id = el.dataset.param;
      if (id) knobEls[id] = el;
    });

    const sliderTrack = document.querySelector(".slider-track");
    const sliderThumb = document.querySelector(".slider-thumb");
    const led = document.querySelector('.led[data-param="os2x"]');

    const applyPowerClass = (on) => {
      const root = document.documentElement;
      root.classList.toggle("powerOn", !!on);
      root.classList.toggle("powerOff", !on);
    };

    const applyKnob = (id, value) => {
      const el = knobEls[id];
      if (!el) return;
      el.style.setProperty("--rot", `${valueToDeg(value)}deg`);
    };

    const applySlider = (value) => {
      if (!sliderTrack || !sliderThumb) return;

      const w = sliderTrack.clientWidth || sliderTrack.getBoundingClientRect().width;
      const tw = sliderThumb.offsetWidth || sliderThumb.getBoundingClientRect().width;
      if (!w || !tw) return;

      const min = tw / 2;
      const max = w - tw / 2;
      const x = min + clamp01(value) * (max - min);
      sliderThumb.style.left = `${x}px`;
    };

    const applyLed = (value) => {
      const on = value >= 0.5;
      if (led) led.classList.toggle("isOn", on);
      applyPowerClass(on);
    };

    const applyParam = (id, value) => {
      if (!PARAMS.has(id)) return;
      const v = id === "os2x" ? quantizeOs2x(value) : clamp01(value);
      state[id] = v;

      if (id === "ceiling") { applySlider(v); return; }
      if (id === "os2x")    { applyLed(v);    return; }
      applyKnob(id, v);
    };

    const setParam = (id, value, fromHost = false) => {
      if (!PARAMS.has(id)) return;
      const v = id === "os2x" ? quantizeOs2x(value) : clamp01(value);

      applyParam(id, v);
      if (!fromHost) postParam(id, v);
    };

    const setSliderFromClientX = (clientX) => {
      if (!sliderTrack) return;
      const rect = sliderTrack.getBoundingClientRect();
      if (!rect.width) return;
      const raw = (clientX - rect.left) / rect.width;
      setParam("ceiling", clamp01(raw), false);
    };

    // ===== KNOBS =====
    document.querySelectorAll(".small-knob, .drive-knob").forEach((el) => {
      el.addEventListener("pointerdown", (e) => {
        if (e.pointerType === "mouse" && e.button !== 0) return;
        const id = el.dataset.param;
        if (!id) return;

        e.preventDefault();
        e.stopPropagation();

        const startY = e.clientY;
        const startValue = state[id] ?? 0.5;
        const pid = e.pointerId;

        try { el.setPointerCapture(pid); } catch {}

        const onMove = (ev) => {
          if (ev.pointerId !== pid) return;
          const dy = startY - ev.clientY;
          setParam(id, clamp01(startValue + dy / 200), false);
        };

        const onUp = (ev) => {
          if (ev.pointerId !== pid) return;
          try { el.releasePointerCapture(pid); } catch {}
          el.removeEventListener("pointermove", onMove);
          el.removeEventListener("pointerup", onUp);
          el.removeEventListener("pointercancel", onUp);
        };

        el.addEventListener("pointermove", onMove);
        el.addEventListener("pointerup", onUp);
        el.addEventListener("pointercancel", onUp);
      }, { passive:false });

      el.addEventListener("dblclick", () => {
        const id = el.dataset.param;
        if (!id || DEFAULTS[id] === undefined) return;
        setParam(id, DEFAULTS[id], false);
      });

      el.addEventListener("wheel", (e) => {
        const id = el.dataset.param;
        if (!id) return;
        e.preventDefault();

        const dir = e.deltaY < 0 ? 1 : -1;
        let step = 0.01;
        if (e.shiftKey) step = 0.05;
        if (e.altKey) step = 0.002;

        setParam(id, clamp01((state[id] ?? 0.5) + dir * step), false);
      }, { passive:false });
    });

    // ===== SLIDER =====
    if (sliderTrack && sliderThumb) {
      const startSlider = (e) => {
        if (e.pointerType === "mouse" && e.button !== 0) return;

        e.preventDefault();
        e.stopPropagation();

        const pid = e.pointerId;
        const target = e.currentTarget;

        try { target.setPointerCapture(pid); } catch {}
        setSliderFromClientX(e.clientX);

        const onMove = (ev) => {
          if (ev.pointerId !== pid) return;
          setSliderFromClientX(ev.clientX);
        };

        const onUp = (ev) => {
          if (ev.pointerId !== pid) return;
          try { target.releasePointerCapture(pid); } catch {}
          target.removeEventListener("pointermove", onMove);
          target.removeEventListener("pointerup", onUp);
          target.removeEventListener("pointercancel", onUp);
        };

        target.addEventListener("pointermove", onMove);
        target.addEventListener("pointerup", onUp);
        target.addEventListener("pointercancel", onUp);
      };

      sliderTrack.addEventListener("pointerdown", startSlider, { passive:false });
      sliderThumb.addEventListener("pointerdown", startSlider, { passive:false });
    }

    // ===== LED ONLY (os2x) =====
    const toggleOs2x = () => {
      const next = quantizeOs2x((state.os2x ?? 0) >= 0.5 ? 0 : 1);
      state.os2x = next;
      applyLed(next); // instant feedback
      postParam("os2x", next);
    };

    if (led) {
      led.addEventListener("pointerdown", (e) => {
        if (e.pointerType === "mouse" && e.button !== 0) return;

        e.preventDefault();
        e.stopPropagation();

        led.classList.add("isPressed");
        toggleOs2x();
      }, { passive:false });

      led.addEventListener("pointerup", () => { led.classList.remove("isPressed"); }, { passive:true });
      led.addEventListener("pointercancel", () => { led.classList.remove("isPressed"); }, { passive:true });
      led.addEventListener("lostpointercapture", () => { led.classList.remove("isPressed"); }, { passive:true });
    }

    // ===== Host -> UI =====
    window.__setParam = (id, value) => {
      if (!PARAMS.has(id)) return;
      const num = Number(value);
      if (Number.isNaN(num)) return;

      setParam(id, num, true);
    };

    // Flush queued host updates
    while (__hostQueue.length) {
      const [id, v] = __hostQueue.shift();
      try { window.__setParam(id, v); } catch {}
    }

    const refreshUI = () => {
      Object.keys(state).forEach((id) => applyParam(id, state[id]));
    };

    window.addEventListener("resize", refreshUI);
    requestAnimationFrame(() => {
      refreshUI();
      requestAnimationFrame(refreshUI);
    });

    applyPowerClass((state.os2x ?? 0) >= 0.5);
  };

  if (document.readyState === "loading") {
    window.addEventListener("DOMContentLoaded", boot, { once:true });
  } else {
    boot();
  }
})();


// =======================
// Credits Modal
// =======================
(() => {
  const btn = document.getElementById("creditBtn");
  const modal = document.getElementById("creditModal");
  const closeBtn = document.getElementById("creditClose");

  if (!btn || !modal || !closeBtn) return;

  const open = () => {
    modal.classList.add("isOpen");
    modal.setAttribute("aria-hidden", "false");
    closeBtn.focus?.();
  };

  const close = () => {
    modal.classList.remove("isOpen");
    modal.setAttribute("aria-hidden", "true");
    btn.focus?.();
  };

  btn.addEventListener("click", open);
  closeBtn.addEventListener("click", close);

  // click outside modal to close
  modal.addEventListener("click", (e) => {
    if (e.target === modal) close();
  });

  // Esc to close
  window.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && modal.classList.contains("isOpen")) close();
  });
})();
