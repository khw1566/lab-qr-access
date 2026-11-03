// Run after installing @resvg/resvg-js in a local tools directory.
// node tools/render_icon_sources.cjs <absolute node_modules/@resvg/resvg-js>
const fs = require('fs');
const path = require('path');
const {Resvg} = require(process.argv[2] || '@resvg/resvg-js');
const source = path.resolve(__dirname, '../assets/source');
for (const name of fs.readdirSync(source).filter(name => name.endsWith('.svg'))) {
  const svg = fs.readFileSync(path.join(source, name), 'utf8');
  const png = new Resvg(svg, {fitTo: {mode: 'width', value: 512}}).render().asPng();
  fs.writeFileSync(path.join(source, name.replace(/\.svg$/, '.png')), png);
}
console.log('Rendered checked-in SVG icon sources at 512px.');
