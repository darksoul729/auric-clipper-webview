// Auric Z-CLIP WebUI - app.js

(() => {
  const PARAMS = new Set(["pre","trim","satclip","mix","drive","ceiling","os2x","power"]);
  const DEFAULTS = { pre:0.35, trim:0.35, satclip:0.5, mix:1.0, drive:0.5, ceiling:0.7, os2x:0, power:1 };

  const clamp01 = (v) => Math.min(1, Math.max(0, v));
  const valueToDeg = (v) => -135 + v * 270;
  const quantizeToggle = (v) => (v >= 0.5 ? 1 : 0);

  // Queue host calls if __setParam comes before DOM ready
  const __hostQueue = [];
  const __earlySetParam = (id, value) => { __hostQueue.push([id, value]); };
  if (typeof window.__setParam !== "function") window.__setParam = __earlySetParam;

  const postEvent = (data) => {
    try {
      if (window.juce && typeof window.juce.postMessage === "function") {
        window.juce.postMessage(JSON.stringify(data));
      }
    } catch {}
  };

  const postParam = (id, value) => postEvent({ type: "param", id, value });

  const boot = () => {
    // Initial UI state (host overrides via __setParam)
    const state = { pre:0.5, trim:0.5, satclip:0.5, mix:0.5, drive:0.5, ceiling:0.5, os2x:0, power:1 };

    const knobEls = {};
    document.querySelectorAll(".small-knob, .drive-knob").forEach((el) => {
      const id = el.dataset.param;
      if (id) knobEls[id] = el;
    });

    const sliderTrack = document.querySelector(".slider-track");
    const sliderThumb = document.querySelector(".slider-thumb");
    const led = document.querySelector('.led[data-param="power"]');
    const btn2x = document.getElementById("btn-2x");

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

    const applyPower = (value) => {
      const on = value >= 0.5;
      if (led) led.classList.toggle("isOn", on);
      applyPowerClass(on);
    };

    const applyOs2x = (value) => {
      const on = value >= 0.5;
      if (btn2x) btn2x.classList.toggle("isOn", on);
    };

    const applyParam = (id, value) => {
      if (!PARAMS.has(id)) return;
      const isToggle = id === "os2x" || id === "power";
      const v = isToggle ? quantizeToggle(value) : clamp01(value);
      state[id] = v;

      if (id === "ceiling") { applySlider(v); return; }
      if (id === "power")   { applyPower(v);  return; }
      if (id === "os2x")    { applyOs2x(v);   return; }
      applyKnob(id, v);
    };

    const setParam = (id, value, fromHost = false) => {
      if (!PARAMS.has(id)) return;
      const isToggle = id === "os2x" || id === "power";
      const v = isToggle ? quantizeToggle(value) : clamp01(value);

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

    // ===== TOGGLES =====
    const togglePower = () => {
      const next = quantizeToggle((state.power ?? 1) >= 0.5 ? 0 : 1);
      setParam("power", next, false);
    };

    const toggleOs2x = () => {
      const next = quantizeToggle((state.os2x ?? 0) >= 0.5 ? 0 : 1);
      setParam("os2x", next, false);
    };

    const bindToggle = (el, handler) => {
      if (!el) return;
      el.addEventListener("pointerdown", (e) => {
        if (e.pointerType === "mouse" && e.button !== 0) return;

        e.preventDefault();
        e.stopPropagation();

        el.classList.add("isPressed");
        handler();
      }, { passive:false });

      el.addEventListener("pointerup", () => { el.classList.remove("isPressed"); }, { passive:true });
      el.addEventListener("pointercancel", () => { el.classList.remove("isPressed"); }, { passive:true });
      el.addEventListener("lostpointercapture", () => { el.classList.remove("isPressed"); }, { passive:true });
    };

    bindToggle(led, togglePower);
    bindToggle(btn2x, toggleOs2x);

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

    let lastUISize = { w: 0, h: 0 };
    const reportUISize = () => {
      const root = document.querySelector(".plugin-interface");
      if (!root) return;
      const rect = root.getBoundingClientRect();
      const w = Math.round(rect.width);
      const h = Math.round(rect.height);
      if (!w || !h) return;
      if (w === lastUISize.w && h === lastUISize.h) return;
      lastUISize = { w, h };
      postEvent({ type: "uiSize", width: w, height: h });
    };

    window.addEventListener("resize", () => {
      refreshUI();
      reportUISize();
    });
    requestAnimationFrame(() => {
      refreshUI();
      reportUISize();
      requestAnimationFrame(() => {
        refreshUI();
        reportUISize();
      });
    });

    applyPowerClass((state.power ?? 1) >= 0.5);
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
