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

  const doReset = () => {
    fetch('/api/reset');
    console.log("Reloading in 5 seconds");
    // Reload page in 5 seconds.
    setTimeout(() => {location.reload();}, 5000);
  };

  // Handles a reset request.
  const onResetSubmit = (event) => {
    event.preventDefault();
    doReset();
  };

  // Handles a Firebase config request.
  const onConfigSubmit = (event) => {
    event.preventDefault();
    const form = event.target;
    const data = {};
    [...form.elements].forEach((input) => {
      if (input.name) {
        data[input.name] = input.value;
      }
    });
    postToUrl('/api/config', {prefix: form.name, config_data: data});

    setTimeout(doReset, 5000);
  };

  const updateStats = async () => {
    try {
      const response = await fetch("/api/stats");
      const stats = await response.json();

      const stats_element = document.getElementById('stats');
      while (stats_element.firstChild) {
        stats_element.removeChild(stats_element.firstChild);
      }

      // Iterate through stats and dump into list items.
      for (let key in stats) {
        const li = document.createElement('li');
        li.textContent = `${key}: ${stats[key]}`;
        stats_element.appendChild(li);
      }
    } catch(error) {
     displayResult('Exception', error.message);
    }
  };

  const loadValues = async () => {
    const response = await fetch("/api/config");
    const config_values = await response.json();
    for (const [full_key, value] of Object.entries(config_values)) {
      const [prefix, key] = full_key.split(":");
      const form = document.forms[prefix];
      if (form) {
        const input = form.elements[key];
        if (input) {
          input.value = value;
        }
      }
    }
  };

  //// Init code
  window.addEventListener('load', () => {
    document.forms.iotz.addEventListener('submit', onConfigSubmit);
    document.forms.wifi.addEventListener('submit', onConfigSubmit);
    document.forms.reset.addEventListener('submit', onResetSubmit);
    document.forms.fb.addEventListener('submit', onConfigSubmit);
    document.forms.log.addEventListener('submit', onConfigSubmit);
    setInterval(updateStats, 10000);
    loadValues();
  });
})();
