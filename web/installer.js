const status = document.querySelector('#build-status');
const button = document.querySelector('#install-button');
const installer = document.querySelector('#installer');

async function json(path) {
  const response = await fetch(path, {cache: 'no-store', signal: AbortSignal.timeout(15000)});
  if (!response.ok) throw new Error(`${path}: ${response.status}`);
  return response.json();
}

async function ready() {
  try {
    const [info, manifest] = await Promise.all([json('./build-info.json'), json('./manifest.json')]);
    const build = manifest.builds?.[0];
    const part = build?.parts?.[0];
    if (build?.chipFamily !== 'ESP32-S3' || build.parts.length !== 1 || part.offset !== 0 ||
        manifest.version !== info.firmware_version || part.path !== info.installer_file) {
      throw new Error('manifest mismatch');
    }
    const response = await fetch(new URL(part.path, location.href), {signal: AbortSignal.timeout(15000)});
    if (!response.ok) throw new Error(`firmware: ${response.status}`);
    const data = await response.arrayBuffer();
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', data)),
      byte => byte.toString(16).padStart(2, '0')).join('');
    if (data.byteLength !== info.installer_bytes || hash !== info.installer_sha256) {
      throw new Error('firmware mismatch');
    }
    // Bind the installer to the bytes verified above, even across a deployment.
    const firmwareUrl = URL.createObjectURL(new Blob([data], {type: 'application/octet-stream'}));
    build.parts[0].path = firmwareUrl;
    const manifestUrl = URL.createObjectURL(new Blob([JSON.stringify(manifest)], {type: 'application/json'}));
    installer.setAttribute('manifest', manifestUrl);
    await Promise.race([
      customElements.whenDefined('esp-web-install-button'),
      new Promise((_, reject) => setTimeout(() => reject(new Error('installer unavailable')), 15000)),
    ]);
    status.textContent = `${info.firmware_version} ・ 準備できました。CoreS3を接続して書き込んでください。`;
    button.disabled = false;
  } catch (error) {
    console.error(error);
    status.textContent = '書き込みデータを読み込めませんでした。通信を確認して、ページを再読み込みしてください。';
    button.disabled = true;
  }
}
ready();
