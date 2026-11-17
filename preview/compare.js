'use strict';

(() => {
  const boardDiameter = 44.5;
  const referenceLength = 50;
  const nominalPixelsPerMm = 96 / 25.4;
  const storageKey = 'attitude-preview-size-v1';
  const get = id => document.getElementById(id);

  const screenSpace = get('screenSpace');
  const screen = get('screen');
  const canvas = get('hud');
  const still = get('stillDial');
  const diameter = get('diameter');
  const slider = get('diameterSlider');
  const fitButton = get('fitPreview');
  const sizeStatus = get('sizeStatus');
  const dialog = get('sizeDialog');
  const measured = get('measured');
  const ruler = get('ruler');
  const resetCalibration = get('resetSizeCalibration');
  const tabs = [...document.querySelectorAll('[data-dial]')];
  const labels = {
    airspeed: '空速表 · 86 km/h · 固件定格',
    attitude: '圆形姿态仪 · 俯仰 5° / 横滚 0° · 固件定格',
    altimeter: '高度计 · 275 m · 固件定格',
    hud: 'HUD · 可调参数'
  };

  let stored;
  try {
    stored = JSON.parse(localStorage.getItem(storageKey));
  } catch {
    // Opening the preview from disk can restrict storage in some browsers.
  }

  const constrain = value => Math.min(Number(diameter.max), Math.max(Number(diameter.min), value));
  let diameterMm = Number.isFinite(stored?.diameterMm) ? constrain(stored.diameterMm) : boardDiameter;
  let calibration = Number.isFinite(stored?.calibration?.pixelsPerMm) &&
    stored.calibration.pixelsPerMm > 0 ? stored.calibration : null;
  let fit = false;
  let selected = -1;

  function storeSize() {
    try {
      localStorage.setItem(storageKey, JSON.stringify({diameterMm, calibration}));
    } catch {
      // Size still works for the current page.
    }
  }

  const pixelRatio = () => window.devicePixelRatio || 1;
  const isCalibrated = () => calibration?.pixelRatio === pixelRatio();
  const pixelsPerMm = () => isCalibrated() ? calibration.pixelsPerMm : nominalPixelsPerMm;
  function refreshSize() {
    screenSpace.style.setProperty('--dial-diameter', `${diameterMm * pixelsPerMm()}px`);
    ruler.style.setProperty('--ruler-width', `${referenceLength * pixelsPerMm()}px`);
    screenSpace.dataset.size = fit ? 'fit' : 'physical';
    fitButton.setAttribute('aria-pressed', String(fit));
    const profile = isCalibrated() ? '已校准' : '待校准';
    sizeStatus.textContent = fit ? '放大预览 · 再点一次返回实物尺寸' : `直径 ${diameterMm.toFixed(1)} mm · ${profile}`;
  }

  function setDiameter(value, normalizeInput = true) {
    if (Number.isFinite(value)) {
      diameterMm = Math.round(constrain(value) * 10) / 10;
      fit = false;
      storeSize();
    }
    slider.value = diameterMm.toFixed(1);
    if (normalizeInput) diameter.value = slider.value;
    refreshSize();
  }

  diameter.addEventListener('input', () => {
    if (diameter.validity.valid && Number.isFinite(diameter.valueAsNumber)) {
      setDiameter(diameter.valueAsNumber, false);
    }
  });
  diameter.addEventListener('blur', () => setDiameter(diameter.valueAsNumber));
  diameter.addEventListener('keydown', event => {
    if (event.key === 'Enter') diameter.blur();
  });
  slider.addEventListener('input', () => setDiameter(slider.valueAsNumber));
  get('boardSize').addEventListener('click', () => setDiameter(boardDiameter));
  fitButton.addEventListener('click', () => {
    fit = !fit;
    refreshSize();
  });
  get('calibrateSize').addEventListener('click', () => {
    refreshSize();
    resetCalibration.hidden = !calibration;
    measured.value = '';
    dialog.showModal();
  });
  get('cancelSize').addEventListener('click', () => dialog.close());
  resetCalibration.addEventListener('click', () => {
    calibration = null;
    storeSize();
    refreshSize();
    dialog.close();
  });
  get('sizeCalibration').addEventListener('submit', event => {
    event.preventDefault();
    if (!measured.validity.valid) return;
    calibration = {
      pixelsPerMm: pixelsPerMm() * referenceLength / measured.valueAsNumber,
      pixelRatio: pixelRatio()
    };
    fit = false;
    storeSize();
    refreshSize();
    dialog.close();
  });
  window.addEventListener('resize', refreshSize);
  slider.value = diameter.value = diameterMm.toFixed(1);
  refreshSize();

  function select(index) {
    if (index === selected) return;
    selected = index;
    const dial = tabs[index].dataset.dial;
    const showHud = dial === 'hud';
    tabs.forEach((tab, i) => tab.setAttribute('aria-pressed', String(i === index)));
    canvas.hidden = !showHud;
    still.hidden = showHud;
    get('hudControls').hidden = !showHud;
    get('reset').hidden = !showHud;
    get('save').hidden = !showHud;
    get('motionState').hidden = !showHud;

    const saveStill = get('saveStill');
    saveStill.hidden = showHud;
    if (!showHud) {
      const source = `dials/${dial}.png`;
      still.alt = labels[dial];
      still.src = source;
      saveStill.href = source;
    }
    get('dialCaption').textContent = labels[dial];
    canvas.dispatchEvent(new Event('dial-visibility'));
  }

  tabs.forEach((tab, i) => tab.addEventListener('click', () => select(i)));
  select(0);
  let gesture;
  screen.addEventListener('pointerdown', event => {
    if (event.pointerType !== 'touch' || !event.isPrimary) return;
    gesture = {id:event.pointerId, x:event.clientX, y:event.clientY};
    screen.setPointerCapture(event.pointerId);
  });
  screen.addEventListener('pointercancel', () => { gesture = null; });
  screen.addEventListener('pointerup', event => {
    if (event.pointerId !== gesture?.id) return;
    const dx = event.clientX - gesture.x, dy = event.clientY - gesture.y;
    gesture = null;
    if (Math.abs(dy) < 26 || Math.abs(dy) <= Math.abs(dx)) return;
    select((selected + (dy < 0 ? 1 : -1) + tabs.length) % tabs.length);
  });

  get('fullscreen').addEventListener('click', async () => {
    try {
      await screenSpace.requestFullscreen();
    } catch {
      sizeStatus.textContent = '浏览器未允许全屏';
    }
  });
  get('exitFullscreen').addEventListener('click', () => document.exitFullscreen());
})();
