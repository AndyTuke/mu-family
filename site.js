// Navigation: Products dropdown (desktop) and menu button (mobile).
(function () {
  var nav = document.querySelector('nav');
  if (!nav) return;
  var toggle = nav.querySelector('.nav-toggle');
  var dd = nav.querySelector('.nav-dd');
  var ddBtn = nav.querySelector('.nav-dd-btn');

  function setMenu(open) {
    nav.classList.toggle('open', open);
    if (toggle) toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
  }
  function setDd(open) {
    if (!dd) return;
    dd.classList.toggle('open', open);
    if (ddBtn) ddBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
  }

  if (toggle) toggle.addEventListener('click', function () { setMenu(!nav.classList.contains('open')); });
  if (ddBtn) ddBtn.addEventListener('click', function () { setDd(!dd.classList.contains('open')); });
  document.addEventListener('click', function (e) {
    if (dd && !dd.contains(e.target)) setDd(false);
  });
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') { setDd(false); setMenu(false); }
  });
  // Close the mobile menu after choosing an in-page link.
  nav.querySelectorAll('.nav-links a').forEach(function (a) {
    a.addEventListener('click', function () { setMenu(false); setDd(false); });
  });
})();
