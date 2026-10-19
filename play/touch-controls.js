// touch-controls: on-screen controls and page layout for pixel-art web games.
// Kept in https://github.com/eralpkaraduman/kurban-bayrami (port/web/).
// Time Escape downloads this file from there at a pinned commit.
//
// One call sets up the page around a game canvas:
//
//   var controls = TouchControls.setup({
//     canvas: document.getElementById('canvas'),
//     frame: document.getElementById('frame'),   // the canvas's parent; the bar goes under the canvas
//     dpad: { left: 263, right: 262, up: 265, down: 264 },  // key codes; leave a direction out to hide it
//     stick: false,                              // optional: arrow buttons instead of the thumb stick
//     buttons: [ { label: 'X', caption: 'Jump', key: 88 }, ... ],
//     onKey: function (key, down) { ... },       // called when a button goes down or up
//     tapClicks: true,                           // optional: a tap on the game is a mouse click
//   });
//   var Module = { canvas: canvas, postRun: [controls.fit] };  // fit once the game has sized its canvas
//
// On touch screens it adds a control bar under the game, fits the game to
// the full width above it, and locks the page against zooming and scrolling.
// Elsewhere it scales the game by a whole number of physical pixels, so the
// pixel art stays sharp, as long as that gives at least 2x. Add ?touch=1 to
// the URL to force the touch layout.
(function () {
  'use strict';

  function shouldShow() {
    if (/[?&]touch=1\b/.test(location.search)) return true;
    if (/[?&]touch=0\b/.test(location.search)) return false;
    return window.matchMedia('(pointer: coarse)').matches;
  }

  function el(tag, className, parent) {
    var node = document.createElement(tag);
    node.className = className;
    if (parent) parent.appendChild(node);
    return node;
  }

  function addButton(parent, className, key, label, caption) {
    var button = el('div', 'tc-button ' + className, parent);
    button.dataset.key = String(key);
    button.setAttribute('role', 'button');
    button.setAttribute('aria-label', caption || label);
    el('span', 'tc-label', button).textContent = label;
    if (caption) el('span', 'tc-caption', button).textContent = caption;
    return button;
  }

  // Builds the bar inside host. Several fingers work at once, and a finger
  // can slide from one button to another without lifting.
  function attach(host, options) {
    host.classList.add('tc-console');
    var bar = el('div', 'tc-bar', host);
    var dpadKeys = options.dpad || {};
    var dpad = el('div', 'tc-dpad', bar);
    var knob = null;
    // With up or down, a thumb stick takes the D-pad's place, unless the
    // game asks for arrows. It sends the same arrow keys, two at once on a
    // diagonal. Left and right alone stay two arrow buttons.
    if (options.stick !== false && (dpadKeys.up != null || dpadKeys.down != null)) {
      dpad.classList.add('tc-stick');
      dpad.setAttribute('role', 'group');
      dpad.setAttribute('aria-label', 'stick');
      knob = el('div', 'tc-knob', dpad);
    } else {
      // Without up and down, the D-pad is a single compact row.
      if (dpadKeys.up == null && dpadKeys.down == null) dpad.classList.add('tc-dpad-row');
      var arrows = { up: '▲', left: '◀', right: '▶', down: '▼' };
      Object.keys(arrows).forEach(function (dir) {
        if (dpadKeys[dir] != null) addButton(dpad, 'tc-' + dir, dpadKeys[dir], arrows[dir], dir);
      });
    }
    var actions = el('div', 'tc-actions', bar);
    // Three or more round buttons are made smaller, so they fit beside a
    // full D-pad on a phone.
    if ((options.buttons || []).length > 2) actions.classList.add('tc-actions-many');
    (options.buttons || []).forEach(function (b) {
      addButton(actions, 'tc-round', b.key, b.label, b.caption);
    });

    // Which keys each finger holds, and how many fingers hold each key.
    var fingers = new Map();
    var holds = new Map();
    var stickFinger = null;

    function keysAt(x, y) {
      var target = document.elementFromPoint(x, y);
      var button = target && target.closest('[data-key]');
      return button && bar.contains(button) ? [Number(button.dataset.key)] : [];
    }

    // The stick's keys for a finger at x, y. The knob follows the finger up
    // to the edge of the base. Near the centre nothing is pressed. Further
    // out, each of the 8 directions covers 45 degrees: an axis counts when
    // the finger is more than 22.5 degrees towards it.
    var STICK_DEAD_ZONE = 0.25;               // of the base's radius
    var STICK_AXIS = Math.sin(Math.PI / 8);   // sin 22.5 degrees
    function stickKeys(x, y) {
      var r = dpad.getBoundingClientRect();
      var radius = r.width / 2;
      var dx = x - (r.left + radius), dy = y - (r.top + r.height / 2);
      var dist = Math.hypot(dx, dy);
      var travel = radius - knob.offsetWidth / 2;
      var scale = dist > travel ? travel / dist : 1;
      knob.style.transform = 'translate(' + dx * scale + 'px, ' + dy * scale + 'px)';
      if (dist < radius * STICK_DEAD_ZONE) return [];
      var keys = [], axis = dist * STICK_AXIS;
      if (dx > axis && dpadKeys.right != null) keys.push(dpadKeys.right);
      if (dx < -axis && dpadKeys.left != null) keys.push(dpadKeys.left);
      if (dy > axis && dpadKeys.down != null) keys.push(dpadKeys.down);
      if (dy < -axis && dpadKeys.up != null) keys.push(dpadKeys.up);
      return keys;
    }

    function setPressed(key, down) {
      bar.querySelectorAll('[data-key="' + key + '"]').forEach(function (button) {
        button.classList.toggle('tc-pressed', down);
      });
      options.onKey(key, down);
    }

    function move(id, keys) {
      var old = fingers.get(id) || [];
      old.forEach(function (key) {
        if (keys.indexOf(key) >= 0) return;
        var left = holds.get(key) - 1;
        holds.set(key, left);
        if (left === 0) setPressed(key, false);
      });
      keys.forEach(function (key) {
        if (old.indexOf(key) >= 0) return;
        var count = (holds.get(key) || 0) + 1;
        holds.set(key, count);
        if (count === 1) setPressed(key, true);
      });
      fingers.set(id, keys);
    }

    function lift(id) {
      move(id, []);
      fingers.delete(id);
      if (id === stickFinger) {
        stickFinger = null;
        dpad.classList.remove('tc-pressed');
        knob.style.transform = '';
      }
    }

    // A finger that lands on the stick steers it until it lifts, even when
    // it slides off the base. Other fingers press the buttons under them.
    function keysFor(e) {
      return e.pointerId === stickFinger ? stickKeys(e.clientX, e.clientY) : keysAt(e.clientX, e.clientY);
    }

    bar.addEventListener('pointerdown', function (e) {
      e.preventDefault();
      bar.setPointerCapture(e.pointerId); // keep getting this finger's events off the bar
      if (knob && stickFinger === null && dpad.contains(e.target)) {
        stickFinger = e.pointerId;
        dpad.classList.add('tc-pressed');
      }
      move(e.pointerId, keysFor(e));
    });
    bar.addEventListener('pointermove', function (e) {
      if (fingers.has(e.pointerId)) move(e.pointerId, keysFor(e));
    });
    bar.addEventListener('pointerup', function (e) { lift(e.pointerId); });
    bar.addEventListener('pointercancel', function (e) { lift(e.pointerId); });
    bar.addEventListener('contextmenu', function (e) { e.preventDefault(); });

    // Never leave a key stuck down when the page loses focus.
    function releaseAll() { Array.from(fingers.keys()).forEach(lift); }
    window.addEventListener('blur', releaseAll);
    document.addEventListener('visibilitychange', function () { if (document.hidden) releaseAll(); });

    return bar;
  }

  // True when the game page sits in a frame inside another page, like the
  // cover page or an itch.io page.
  function isEmbedded() {
    try {
      return window.self !== window.top;
    } catch (e) {
      return true; // a cross-origin parent hides window.top
    }
  }

  // The usual mobile web game setup: the page never zooms, selects text or
  // opens long-press menus, so every touch goes to the game. CSS covers most
  // of it (.tc-mobile in touch-controls.css). iOS Safari ignores
  // user-scalable=no for pinch, so its gesture events are blocked too.
  //
  // Standalone, the page never scrolls either. Embedded in another page, an
  // up or down swipe over the game scrolls that page instead, so readers can
  // reach the text below the game. The control bar never scrolls, either way.
  function lockPage() {
    var embedded = isEmbedded();
    document.documentElement.classList.add('tc-mobile');
    document.documentElement.classList.toggle('tc-embedded', embedded);
    var block = function (e) { e.preventDefault(); };
    ['gesturestart', 'gesturechange', 'gestureend', 'dblclick', 'contextmenu'].forEach(function (type) {
      document.addEventListener(type, block, { passive: false });
    });
    // A second finger landing anywhere must not start a pinch. These page
    // listeners run in the capture phase, before setup() keeps touches away
    // from the game's own canvas listeners.
    document.addEventListener('touchmove', function (e) {
      if (e.touches.length > 1) e.preventDefault();
    }, { passive: false, capture: true });
    // iOS Safari has no setting to turn off its edge swipe back and forward,
    // and ignores overscroll-behavior for it. Since iOS 13.4, cancelling
    // touchstart blocks that swipe. The standalone page never scrolls and has
    // no links, so every touch start can be cancelled. Pointer events, which
    // the control bar uses, still arrive. Embedded, cancelling would also stop
    // the outer page from scrolling, so it is left alone.
    if (!embedded) document.addEventListener('touchstart', block, { passive: false, capture: true });
  }

  // Space the bar takes under the game besides its buttons, matching
  // touch-controls.css: 12 px above the buttons and 28 px below.
  var UNDER_PADDING = 12 + 28;

  // The least height the bar needs under the game. The bar may grow beyond it.
  function minHeight(bar) {
    var dpad = bar.querySelector('.tc-dpad'), actions = bar.querySelector('.tc-actions');
    return Math.max(dpad.offsetHeight, actions.offsetHeight) + UNDER_PADDING;
  }

  // Sizes the canvas with CSS. The canvas's own width and height are the
  // game's resolution, set by the game itself.
  function fit(canvas, bar) {
    var w = canvas.width, h = canvas.height;
    if (!w || !h) return;
    var cssW;
    if (bar) {
      // Touch layout. The bar goes under the game when that costs the game
      // no size, as in portrait. Otherwise, as in landscape, the game takes
      // the largest size that fits the whole screen, and the controls float
      // over it in the lower corners. The body's padding holds the notch and
      // home-indicator insets.
      var body = getComputedStyle(document.body);
      var roomW = document.body.clientWidth - parseFloat(body.paddingLeft) - parseFloat(body.paddingRight);
      var roomH = document.body.clientHeight - parseFloat(body.paddingTop) - parseFloat(body.paddingBottom);
      var fullW = Math.min(roomW, roomH * w / h);
      var underW = Math.min(roomW, (roomH - minHeight(bar)) * w / h);
      var overlay = underW < fullW;
      document.documentElement.classList.toggle('tc-overlay', overlay);
      cssW = overlay ? fullW : underW;
    } else {
      // Keyboard layout: a whole number of physical pixels per game pixel,
      // when that reaches at least 2. Below 2x, rounding down would waste up
      // to half the window, so the game fills the window at a fractional
      // scale instead. This also shrinks a game larger than the window.
      var dpr = window.devicePixelRatio || 1;
      var fill = Math.min(innerWidth / w, innerHeight / h);
      var scale = Math.floor(fill * dpr);
      cssW = scale >= 2 ? w * scale / dpr : w * fill;
    }
    canvas.style.width = cssW + 'px';
    canvas.style.height = (cssW * h / w) + 'px';
  }

  // A tap on the canvas reaches the game as a left click, for games that
  // read the mouse, like a title screen button or "click to go on". The game
  // never sees the touch itself, so a swipe still scrolls an embedded page.
  // Only a short touch that stays in place counts. The click goes to the
  // canvas as mouse events at the tap's position: a move and a press when the
  // finger lifts, then the release a little later, so the game sees the press
  // in at least one frame.
  var TAP_SLOP = 10;        // px a tap may move
  var TAP_MAX_MS = 500;     // longer is a hold, not a tap
  var CLICK_MS = 100;       // press to release

  function forwardTaps(frame, canvas) {
    var start = null;
    function send(type, x, y) {
      canvas.dispatchEvent(new MouseEvent(type, {
        bubbles: true, cancelable: true, view: window, clientX: x, clientY: y,
        screenX: x, screenY: y, button: 0, buttons: type === 'mouseup' ? 0 : 1,
      }));
    }
    function moved(t) {
      return Math.abs(t.clientX - start.x) > TAP_SLOP || Math.abs(t.clientY - start.y) > TAP_SLOP;
    }
    var listen = function (type, handler, passive) {
      frame.addEventListener(type, function (e) {
        if (e.target === canvas) handler(e);
      }, { capture: true, passive: passive });
    };
    listen('touchstart', function (e) {
      var t = e.touches[0];
      start = e.touches.length === 1 ? { x: t.clientX, y: t.clientY, time: Date.now() } : null;
    }, true);
    listen('touchmove', function (e) {
      if (start && (e.touches.length > 1 || moved(e.touches[0]))) start = null;
    }, true);
    listen('touchcancel', function () { start = null; }, true);
    listen('touchend', function (e) {
      var t = e.changedTouches[0];
      var tap = start && e.touches.length === 0 && !moved(t) && Date.now() - start.time <= TAP_MAX_MS;
      start = null;
      if (!tap) return;
      e.preventDefault(); // no second click made up by the browser
      send('mousemove', t.clientX, t.clientY);
      send('mousedown', t.clientX, t.clientY);
      setTimeout(function () { send('mouseup', t.clientX, t.clientY); }, CLICK_MS);
    }, false);
  }

  function setup(options) {
    var bar = null;
    if (shouldShow()) {
      lockPage();
      bar = attach(options.frame, options);
      // raylib listens for touches on the canvas and cancels them, which
      // stops an embedded page from scrolling over the game. The game reads
      // touch only through the bar, so canvas touches never reach raylib.
      ['touchstart', 'touchmove', 'touchend', 'touchcancel'].forEach(function (type) {
        options.frame.addEventListener(type, function (e) {
          if (e.target === options.canvas) e.stopPropagation();
        }, { capture: true, passive: true });
      });
      if (options.tapClicks) forwardTaps(options.frame, options.canvas);
    }
    var refit = function () { fit(options.canvas, bar); };
    window.addEventListener('resize', refit);
    window.addEventListener('orientationchange', function () { setTimeout(refit, 100); });
    if (window.visualViewport) window.visualViewport.addEventListener('resize', refit);
    return { bar: bar, fit: refit };
  }

  window.TouchControls = {
    setup: setup,
    attach: attach,
    lockPage: lockPage,
    shouldShow: shouldShow,
    minHeight: minHeight,
    fit: fit,
  };
})();
