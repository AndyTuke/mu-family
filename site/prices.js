// Prices and checkout links: fills the page from prices.json (written from Lemon Squeezy).
// The text and links already in the page are the fallback if the file cannot be loaded.
(function () {
  if (!window.fetch) return;
  fetch('prices.json', { cache: 'no-cache' })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(); })
    .then(function (data) {
      var products = (data && data.products) || {};
      document.querySelectorAll('[data-price]').forEach(function (el) {
        var p = products[el.getAttribute('data-price')];
        if (p && p.priceText) el.textContent = p.priceText;
      });
      document.querySelectorAll('[data-checkout]').forEach(function (el) {
        var p = products[el.getAttribute('data-checkout')];
        if (p && p.checkout) el.setAttribute('href', p.checkout);
      });
    })
    .catch(function () { /* keep the built-in text and links */ });
})();
