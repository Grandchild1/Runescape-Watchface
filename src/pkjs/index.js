// PebbleKit JS companion for the RuneScape watchface.
//
// Fetches current weather from Open-Meteo (https://open-meteo.com) using the
// phone's geolocation. Open-Meteo requires no API key. Weather is condensed
// into a temperature (whole degrees C) and a small condition category that
// the watch maps to one of its hand-drawn weather icons:
//   0 = clear, 1 = clouds, 2 = rain, 3 = snow, 4 = storm

function xhrRequest(url, type, callback, errorCallback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (xhr.status >= 200 && xhr.status < 300) {
      callback(xhr.responseText);
    } else if (errorCallback) {
      errorCallback('HTTP status ' + xhr.status);
    }
  };
  xhr.onerror = function () {
    if (errorCallback) errorCallback('Network error');
  };
  xhr.open(type, url);
  xhr.send();
}

// Maps Open-Meteo's WMO weather codes down to the watch's icon categories.
function weatherCodeToCategory(code) {
  if (code === 0) return 0; // clear sky
  if (code >= 1 && code <= 3) return 1; // mainly clear / partly cloudy / overcast
  if (code === 45 || code === 48) return 1; // fog -> render as cloud
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 2; // drizzle / rain / showers
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 3; // snow
  if (code >= 95) return 4; // thunderstorm
  return 1;
}

function sendWeatherToWatch(temperature, category) {
  var dict = {
    'WEATHER_TEMP': temperature,
    'WEATHER_COND': category
  };
  Pebble.sendAppMessage(dict,
    function () {
      console.log('RuneScape watchface: weather sent to watch (' + temperature + 'C, category ' + category + ')');
    },
    function (e) {
      console.log('RuneScape watchface: error sending weather to watch: ' + JSON.stringify(e));
    }
  );
}

function fetchWeather(latitude, longitude) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + encodeURIComponent(latitude) +
    '&longitude=' + encodeURIComponent(longitude) +
    '&current_weather=true&temperature_unit=celsius';

  xhrRequest(url, 'GET', function (responseText) {
    var json = JSON.parse(responseText);
    var temperature = Math.round(json.current_weather.temperature);
    var category = weatherCodeToCategory(json.current_weather.weathercode);
    sendWeatherToWatch(temperature, category);
  }, function (error) {
    console.log('RuneScape watchface: weather fetch failed: ' + error);
  });
}

function locationSuccess(pos) {
  fetchWeather(pos.coords.latitude, pos.coords.longitude);
}

function locationError(err) {
  console.log('RuneScape watchface: location request failed: ' + err.message);
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    { enableHighAccuracy: false, timeout: 15000, maximumAge: 60000 }
  );
}

// Fired once when the JS environment on the phone is ready.
Pebble.addEventListener('ready', function () {
  console.log('RuneScape watchface: PebbleKit JS ready');
  getWeather();
});

// The watch sends an (empty) AppMessage whenever it wants a weather refresh
// (on load, and every 30 minutes thereafter). Treat any inbound message as
// a refresh request.
Pebble.addEventListener('appmessage', function () {
  getWeather();
});
