// UI language: English (the text written in the pages) or Vietnamese.
//
// English text is the key: tr('Send to car') returns the Vietnamese below, or the English
// itself when no translation exists. {name} placeholders are filled from the 2nd argument.
// Static page text is translated by walking the DOM once on load, so the HTML stays plain
// English; text set from JS goes through tr(). State names (NORMAL, FAST…), config.h keys,
// units and messages coming from the server or the car stay in English.
//
// Choice: localStorage 'emocar.lang', else the browser language. The EN | VI switch fills
// the page's #lang element and reloads the page.
'use strict';

const LANG = (() => {
  try { const s = localStorage.getItem('emocar.lang'); if (s === 'en' || s === 'vi') return s; } catch {}
  return /^vi\b/i.test(navigator.language || '') ? 'vi' : 'en';
})();
document.documentElement.lang = LANG;

const VI = {
  // ---------------------------------------------------------------- shared
  'connecting…': 'đang kết nối…',
  'connected {port}': 'đã kết nối {port}',
  'offline': 'mất kết nối',
  'server not reachable': 'không kết nối được server',
  'server not reachable — retrying': 'không kết nối được server — đang thử lại',
  'state': 'trạng thái',
  'State conditions': 'Điều kiện trạng thái',

  // ---------------------------------------------------------------- / (animations)
  'looking for the car…': 'đang tìm xe…',
  'Sensor dashboard →': 'Bảng cảm biến →',
  'State conditions →': 'Điều kiện trạng thái →',
  'display': 'màn hình',
  'On the car': 'Trên xe',
  'Library': 'Thư viện',
  '+ add GIF': '+ thêm GIF',
  'threshold': 'ngưỡng sáng',
  'invert': 'đảo màu',
  'none': 'không',
  'scale': 'co giãn',
  'fit': 'vừa khung',
  'fit_width': 'vừa chiều ngang',
  'fit_height': 'vừa chiều dọc',
  'stretch': 'kéo giãn',
  'zoom': 'thu phóng',
  'pan': 'di chuyển',
  'reset': 'đặt lại',
  'drag the preview to pan (or type x/y: + is right/down) · scroll to zoom · double-click resets':
    'kéo ảnh xem trước để di chuyển (hoặc gõ x/y: + là sang phải/xuống) · cuộn chuột để phóng to/thu nhỏ · nháy đúp để đặt lại',
  'Send to car': 'Gửi lên xe',
  'Close': 'Đóng',
  'on {states}': 'đang dùng cho {states}',
  'car has {id} — pushes will be refused': 'xe đang dùng {id} — gửi lên sẽ bị từ chối',
  'placeholder': 'ảnh có sẵn',
  'placeholder / unknown': 'ảnh có sẵn / không rõ',
  'online on {port} — state {state}, heap {heap} KB, up {up}s': 'đã kết nối qua {port} — trạng thái {state}, RAM trống {heap} KB, đã chạy {up} giây',
  ', display {profile}': ', màn hình {profile}',
  '{frames} frames · {kb} KB': '{frames} khung hình · {kb} KB',
  'offline — ': 'mất kết nối — ',
  'plug the car in over USB': 'cắm xe vào máy qua cáp USB',
  'server error': 'lỗi server',
  '{state} now has: {name}': '{state} đang dùng: {name}',
  '⚠ {name} is already on {states}': '⚠ {name} đã được dùng cho {states}',
  'converting + sending…': 'đang chuyển đổi + gửi…',
  'on the car — {frames} frames, {kb} KB': 'đã lên xe — {frames} khung hình, {kb} KB',
  'failed': 'thất bại',
  'upload failed': 'tải lên thất bại',

  // ---------------------------------------------------------------- WiFi web app (web/)
  '{host} — fw {fw}, state {state}': '{host} — fw {fw}, trạng thái {state}',
  'car not answering — is it on this WiFi?': 'xe không trả lời — xe có đang cùng WiFi không?',
  'loading the converter…': 'đang tải bộ chuyển đổi…',
  'converter failed to load: {err}': 'không tải được bộ chuyển đổi: {err}',
  'Tap a state to change its face': 'Chạm vào một trạng thái để đổi khuôn mặt',
  'now on the car': 'đang trên xe',
  'Choose GIF': 'Chọn GIF',
  'built-in': 'ảnh có sẵn',
  '{name} · {frames} frames · {kb} KB': '{name} · {frames} khung hình · {kb} KB',
  'sending…': 'đang gửi…',
  '{name} is not a GIF or .qgif': '{name} không phải GIF hoặc .qgif',

  // ---------------------------------------------------------------- /configs
  'emocar configs': 'emocar cấu hình',
  '· state conditions': '· điều kiện trạng thái',
  'Animations': 'Ảnh động',
  'Dashboard': 'Bảng cảm biến',
  'orange = not applied ·': 'cam = chưa áp dụng ·',
  'blue = differs from sketch': 'xanh = khác mặc định',
  'Export to config.h': 'Xuất ra config.h',
  'Every setting back to the sketch\'s defaults, on the car': 'Đưa mọi thiết lập trên xe về mặc định',
  'Reset all': 'Đặt lại tất cả',
  'States': 'Trạng thái',
  'Reset to sketch': 'Về mặc định',
  'Apply': 'Áp dụng',
  'priority DIZZY > HAPPY > FAST > NORMAL · an unchecked state is skipped':
    'ưu tiên DIZZY > HAPPY > FAST > NORMAL · trạng thái không được chọn sẽ bị bỏ qua',
  'Timing': 'Thời gian',
  '· all states': '· mọi trạng thái',
  '· off (still editable)': '· đang tắt (vẫn sửa được)',
  'Shake': 'Lắc',
  'Spin': 'Xoay vòng',
  'swings gx': 'số lần lắc gx',
  'ctx': 'vị trí',
  '· need': '· cần',
  'in': 'trong',
  'dps · spin': 'dps · xoay',
  '· roll': '· nghiêng',
  'HAND': 'TRÊN TAY',
  'FLOOR': 'DƯỚI SÀN',

  'Enter hold': 'Chờ chuyển lên',
  'Exit hold': 'Chờ chuyển xuống',
  'Enter RMS': 'Mức rung bắt đầu',
  'Exit RMS': 'Mức rung kết thúc',
  'Vibrate at least': 'Rung liên tục ít nhất',
  'Spin at least': 'Xoay liên tục ít nhất',
  'Swing line': 'Ngưỡng lắc',
  'Re-arm below': 'Ngưỡng đếm lại',
  'Swings': 'Số lần lắc',
  'Window': 'Thời gian đếm',
  'Spin rate': 'Tốc độ xoay',
  'Allowed dip': 'Cho phép chậm lại',
  'Keep DIZZY after': 'Giữ DIZZY sau khi hết',
  'Tilt': 'Góc nghiêng',
  'Tilt window': 'Thời gian nghiêng',
  'Roll gate': 'Lọc cú gõ',
  'Quiet below': 'Ngưỡng nhiễu',
  'Quiet clears': 'Xóa sau khi yên',
  'Still RMS': 'Mức rung đứng yên',
  'Settle': 'Thời gian đặt xuống',

  'The default face, shown whenever no other state applies. Always on.':
    'Khuôn mặt mặc định, hiện khi không có trạng thái nào khác. Luôn bật.',
  'The car is driving on a surface: steady vibration from the wheels. Floor only — never shown while the car is in a hand.':
    'Xe đang chạy trên mặt phẳng: rung đều từ bánh xe. Chỉ khi ở dưới sàn — không bao giờ hiện khi xe đang trên tay.',
  'Shaken hard, or spun in circles. Highest priority: it beats HAPPY, so shaking a held car shows DIZZY.':
    'Bị lắc mạnh, hoặc chạy vòng tròn. Ưu tiên cao nhất: thắng cả HAPPY, nên lắc xe khi đang cầm trên tay sẽ hiện DIZZY.',
  'Picked up and held in a hand. Unticked: a held car shows NORMAL (never FAST).':
    'Được nhấc lên và cầm trên tay. Không chọn: xe đang cầm sẽ hiện NORMAL (không bao giờ FAST).',
  'How long a higher state must be wanted before the face switches up to it (e.g. NORMAL → HAPPY). Lower = snappier; higher = ignores short blips.':
    'Điều kiện của trạng thái cao hơn phải kéo dài bao lâu thì khuôn mặt mới chuyển lên (vd. NORMAL → HAPPY). Thấp = đổi mặt nhanh hơn; cao = bỏ qua các thay đổi rất ngắn.',
  'How long a face stays after its condition ends, before stepping down (e.g. HAPPY → NORMAL). Higher = faces linger longer.':
    'Khuôn mặt còn ở lại bao lâu sau khi điều kiện của nó hết, rồi mới lùi xuống (vd. HAPPY → NORMAL). Cao = khuôn mặt ở lại lâu hơn.',
  'Vibration level the car must pass to count as driving — the RMS of the turn rate on all 3 axes over the last 250 ms, in °/s. On a desk at rest ≈ 0.5. On soft floors (carpet, bed) lower it toward 3.0.':
    'Mức rung xe phải vượt qua để được tính là đang chạy — RMS của tốc độ quay trên cả 3 trục trong 250 ms gần nhất, đơn vị °/s. Nằm yên trên bàn ≈ 0.5. Trên sàn mềm (thảm, giường) hãy giảm về khoảng 3.0.',
  'Vibration below this ends FAST. Must be lower than Enter RMS: the gap between them stops the face flickering when vibration hovers around one value.':
    'Rung dưới mức này thì kết thúc FAST. Phải nhỏ hơn Mức rung bắt đầu: khoảng cách giữa hai mức giúp khuôn mặt không nhấp nháy khi độ rung dao động quanh một giá trị.',
  'Vibration must stay above Enter RMS this long before FAST starts. A tap spikes vibration but never lasts, so this filters taps out.':
    'Rung phải ở trên Mức rung bắt đầu trong khoảng này thì FAST mới bắt đầu. Một cú gõ làm rung vọt lên nhưng không kéo dài, nên cú gõ bị lọc bỏ.',
  'A “swing” = the car turning faster than ± this on one axis. A hard shake goes far past 400 °/s; picking the car up rarely does. Lower = DIZZY triggers more easily.':
    'Một “lần lắc” = xe quay nhanh hơn ± mức này trên một trục. Lắc mạnh vượt xa 400 °/s; nhấc xe lên hiếm khi đạt tới. Thấp = DIZZY dễ xảy ra hơn.',
  'After a swing, that axis must slow below this before the same direction can count again — so one long swing isn\'t counted many times. Must be lower than Swing line.':
    'Sau một lần lắc, trục đó phải chậm lại dưới mức này thì cùng chiều mới được đếm tiếp — để một lần lắc dài không bị đếm nhiều lần. Phải nhỏ hơn Ngưỡng lắc.',
  'How many swings within Window trigger DIZZY (at most 16). Each axis (gx, gy, gz) is counted separately, never added together. Measured: hard shake gx 10, gy 11, gz 5 in 2 s; a pickup 0–1.':
    'Bao nhiêu lần lắc trong Thời gian đếm thì bật DIZZY (tối đa 16). Mỗi trục (gx, gy, gz) đếm riêng, không bao giờ cộng dồn. Đo thực tế: lắc mạnh gx 10, gy 11, gz 5 trong 2 s; nhấc lên 0–1.',
  'Swings older than this stop counting. Longer = a slower shake still triggers DIZZY.':
    'Lần lắc cũ hơn khoảng này thì không được tính nữa. Dài hơn = lắc chậm hơn vẫn bật DIZZY.',
  'Turn rate on the flat (gz, yaw) the car must hold in one direction — the car driving in circles. Reversing direction starts over. Not yet measured on the car.':
    'Tốc độ quay trên mặt phẳng (gz, yaw) xe phải giữ theo một chiều — xe chạy vòng tròn. Đổi chiều thì đếm lại từ đầu. Chưa đo thực tế trên xe.',
  'How long the spin must last before DIZZY. 5000 = five seconds of circling.':
    'Xoay vòng phải kéo dài bao lâu thì bật DIZZY. 5000 = năm giây chạy vòng.',
  'Short drops below Spin rate (a bump, wheel slip) don\'t break the spin if they are shorter than this. Longer drops end it.':
    'Những lần hụt ngắn dưới Tốc độ xoay (va chạm, trượt bánh) không làm gián đoạn nếu ngắn hơn mức này. Hụt lâu hơn thì kết thúc.',
  'How long DIZZY keeps showing after the last swing or the end of the spin. Includes Exit hold, so it can\'t be shorter than Exit hold.':
    'DIZZY còn hiện bao lâu sau lần lắc cuối hoặc sau khi hết xoay. Đã gồm Chờ chuyển xuống, nên không thể ngắn hơn Chờ chuyển xuống.',
  'A car on its wheels can\'t roll sideways; a hand always tilts it. Roll (gy) is added up in both directions, and this many degrees means “in a hand”. Lower = HAPPY triggers more easily, but bumps may count.':
    'Xe đứng trên bánh thì không nghiêng ngang được; tay cầm thì luôn làm xe nghiêng. Độ nghiêng (gy) được cộng dồn theo cả hai chiều, đạt số độ này nghĩa là “đang trên tay”. Thấp = HAPPY dễ xảy ra hơn, nhưng va chạm cũng có thể bị tính.',
  'The roll must add up to Tilt within this time, otherwise the count restarts.':
    'Độ nghiêng phải cộng dồn đủ Góc nghiêng trong khoảng thời gian này, nếu không thì đếm lại.',
  'A roll must last this long before it starts counting at all. Filters out taps and knocks on the car.':
    'Một lần nghiêng phải kéo dài chừng này mới bắt đầu được tính. Lọc bỏ các cú gõ, va vào xe.',
  'Roll rates below this are treated as sensor noise and not added to Tilt.':
    'Tốc độ nghiêng dưới mức này được coi là nhiễu cảm biến và không cộng vào Góc nghiêng.',
  'Being quiet (below Quiet below) this long clears the roll count. The brief stops while a hand turns the car are shorter, so they don\'t clear it.':
    'Yên (dưới Ngưỡng nhiễu) trong khoảng này thì xóa số đếm nghiêng. Những lúc dừng ngắn khi tay xoay xe thì ngắn hơn, nên không bị xóa.',
  'Vibration below this counts as “still” — used to decide the car was put down. At rest ≈ 0.5, held in a palm ≈ 35.':
    'Rung dưới mức này được coi là “đứng yên” — dùng để biết xe đã được đặt xuống. Nằm yên ≈ 0.5, cầm trong lòng bàn tay ≈ 35.',
  'Still for this long = set back down: leaves hand mode, so HAPPY ends and FAST can work again.':
    'Đứng yên trong khoảng này = đã đặt xuống: thoát chế độ trên tay, nên HAPPY kết thúc và FAST hoạt động lại.',

  'on': 'bật',
  'off': 'tắt',
  'sketch default {v}': 'mặc định {v}',
  ' · on car {v}': ' · trên xe {v}',
  'must be below {name} ({v})': 'phải nhỏ hơn {name} ({v})',
  'must be ≥ {name} ({v})': 'phải ≥ {name} ({v})',
  'needs a number ≥ 0': 'cần một số ≥ 0',
  '{n} differ from sketch': '{n} khác mặc định',
  'matches sketch': 'đúng mặc định',
  'Rewrite the #define lines in {path}': 'Ghi lại các dòng #define trong {path}',
  '✓ saved on car': '✓ đã lưu trên xe',
  'car not connected': 'xe chưa kết nối',
  'server not reachable — ': 'không kết nối được server — ',
  'nothing changed': 'không có gì thay đổi',
  'applied {keys} — saved on car': 'đã áp dụng {keys} — đã lưu trên xe',
  'not applied: ': 'chưa áp dụng: ',
  'sketch defaults filled in — Apply to send them': 'đã điền giá trị mặc định — bấm Áp dụng để gửi lên xe',
  'every setting back to the sketch defaults': 'mọi thiết lập đã về mặc định',
  'reset failed: ': 'đặt lại thất bại: ',
  'config.h: {n} line(s) rewritten ({list}) — flash the sketch to make them the defaults':
    'config.h: đã ghi lại {n} dòng ({list}) — nạp lại firmware để chúng thành mặc định',
  'config.h already matches the car': 'config.h đã khớp với xe',
  'export failed: ': 'xuất thất bại: ',

  // ---------------------------------------------------------------- /dashboard
  'emocar sensor': 'emocar cảm biến',
  '· sensor dashboard': '· bảng cảm biến',
  '← Animations': '← Ảnh động',
  'waiting': 'đang chờ',
  'Space': 'Phím cách',
  'Pause': 'Tạm dừng',
  'Resume': 'Tiếp tục',
  'Current pose becomes 0° on every axis': 'Lấy tư thế hiện tại làm gốc 0° cho mọi trục',
  'Zero': 'Đặt gốc',
  'Orientation': 'Tư thế',
  'since Zero / boot': 'tính từ lúc Đặt gốc / bật xe',
  'pitch · about X': 'pitch · quanh trục X',
  'roll · about Y': 'roll · quanh trục Y',
  'yaw · about Z': 'yaw · quanh trục Z',
  'board top': 'mặt trên bo mạch',
  'bottom': 'mặt dưới',
  'zero pose': 'tư thế gốc',
  'Motion (what the states read)': 'Chuyển động (số liệu dùng để chọn trạng thái)',
  'roll travel °': 'tổng nghiêng °',
  'swings / 2 s · DIZZY at 6': 'số lần lắc / 2 s · DIZZY khi đạt 6',
  'spin s': 'thời gian xoay (s)',
  'Rates (deg/s)': 'Tốc độ quay (độ/s)',
  'Bar and chart scale': 'Thang đo của thanh và biểu đồ',
  'Chart window': 'Khoảng thời gian của biểu đồ',
  'Chart scale': 'Thang đo biểu đồ',
  'gz · peak per line, hover to read': 'gz · rê chuột lên biểu đồ để xem số',
  'Motion signals': 'Tín hiệu chuyển động',
  'roll travel° · strips: state, hand': 'tổng nghiêng° · hai dải màu dưới: trạng thái, trên tay',
  'still': 'đứng yên',
  '↺ CCW': '↺ ngược chiều kim đồng hồ',
  '↻ CW': '↻ thuận chiều kim đồng hồ',
  'car connected but sends no telemetry — it may be rebooting (a few seconds), or its firmware predates TELEM / has the gyro off':
    'xe đã kết nối nhưng không gửi dữ liệu — có thể đang khởi động lại (vài giây), hoặc firmware cũ chưa có TELEM / đang tắt gyro',
  'PAUSED — data still arriving': 'ĐÃ TẠM DỪNG — dữ liệu vẫn đang về',
  'LIVE {hz} Hz': 'TRỰC TIẾP {hz} Hz',
  'now': 'bây giờ',
  'floor': 'sàn',
  'not turning': 'không xoay',
  'turning <b>{dir}</b> about {axis} ({name}) at <b>{v}°/s</b>': 'đang xoay <b>{dir}</b> quanh trục {axis} ({name}) với <b>{v}°/s</b>',
  'in a HAND': 'trên TAY',
  'on the floor': 'dưới sàn',
  'zeroing…': 'đang đặt gốc…',
  'zeroed': 'đã đặt gốc',
};

function tr(s, vars) {
  let out = (LANG === 'vi' && VI[s]) || s;
  if (vars) out = out.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? vars[k] : m));
  return out;
}

// The server pages load this in <head>, so it runs on DOMContentLoaded. The WiFi web app
// loads it after the page is up and calls i18nApply() itself once its DOM is built.
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', () => i18nApply());

function i18nApply() {
  if (LANG === 'vi') {
    const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, {
      acceptNode: n => n.parentElement.closest('script, style')
        ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_ACCEPT,
    });
    for (let n; (n = w.nextNode());) {
      const raw = n.nodeValue, key = raw.trim().replace(/\s+/g, ' ');
      if (key && VI[key]) n.nodeValue = raw.match(/^\s*/)[0] + VI[key] + raw.match(/\s*$/)[0];
    }
    for (const el of document.querySelectorAll('[title]')) el.title = tr(el.title);
    document.title = tr(document.title);
  }

  const box = document.getElementById('lang');
  if (!box) return;
  const css = document.createElement('style');
  css.textContent = `
    #lang { display: inline-flex; border: 1px solid var(--edge); border-radius: 6px; overflow: hidden; }
    #lang button { background: none; border: 0; border-radius: 0; margin: 0; padding: 2px 9px;
                   font: 600 12px/1.6 system-ui, sans-serif; color: var(--dim); cursor: pointer; }
    #lang button.on { background: var(--accent); color: #06131f; cursor: default; }`;
  document.head.appendChild(css);
  box.innerHTML = '<button data-l="en" title="English">EN</button><button data-l="vi" title="Tiếng Việt">VI</button>';
  for (const b of box.querySelectorAll('button')) {
    b.classList.toggle('on', b.dataset.l === LANG);
    b.onclick = () => {
      if (b.dataset.l === LANG) return;
      try { localStorage.setItem('emocar.lang', b.dataset.l); } catch {}
      location.reload();
    };
  }
}
