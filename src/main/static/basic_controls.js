const EspCxxControls = {};

(() => {
  // Takes a result promise and displays it.
  const displayResult = (header, text) => {
    const result_box = document.getElementById('result-box');
    result_box.hidden = false;
    const p = document.createElement('p');
    const textNode = document.createTextNode(`${header}: ${text}`);
    p.appendChild(textNode);

    result_box.replaceChild(p, result_box.firstChild);
  };

  // Post to a URL as json and renders the result.
  const postToUrl = (url, blob) => {
    fetch(url, {
      method: 'POST',
      headers: {
            'Content-Type': 'application/json',
      },
      body: JSON.stringify(blob)
    }).then(response => displayResult('Response', JSON.stringify(response.json())))
    .catch(error => displayResult('Exception', error.message));
  };

  // Handles submitting new config data for wifi.
  const onWifiConfigSubmit = (event) => {
    event.preventDefault();
    const form = event.target;
    const update = {
      ssid: form.elements.namedItem('ssid'),
      password: form.elements.namedItem('password'),
    };

    postToUrl('/api/wificonfig', update);
  }

  // Handles a reset request.
  const onResetSubmit = (event) => {
    event.preventDefault();
    postToUrl('/api/reset', {});
    // TODO(awong): Reload page after 5 seconds.
  }

  //// Init code
  window.addEventListener('load', () => {
    document.forms.wificonfig.addEventListener('submit', onWifiConfigSubmit);
    document.forms.reset.addEventListener('submit', onResetSubmit);
  });
})();
