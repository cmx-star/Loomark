import MathJax from 'mathjax';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const input = await new Promise((resolve, reject) => {
  let data = '';
  process.stdin.setEncoding('utf8');
  process.stdin.on('data', chunk => {
    data += chunk;
  });
  process.stdin.on('end', () => resolve(data));
  process.stdin.on('error', reject);
});

function countSvgTags(svg) {
  return (svg.match(/<svg\b/g) ?? []).length;
}

function attributesFor(tag) {
  const attributes = new Map();
  for (const match of tag.matchAll(/([:\w-]+)\s*=\s*"([^"]*)"/g)) {
    attributes.set(match[1], match[2]);
  }
  return attributes;
}

function numericAttribute(attributes, name, fallback) {
  const raw = attributes.get(name);
  if (raw === undefined) {
    return fallback;
  }
  const value = Number.parseFloat(raw);
  return Number.isFinite(value) ? value : fallback;
}

function flattenNestedSvg(svg) {
  const nestedSvgPattern = /<svg\b([^>]*)>((?:(?!<svg\b|<\/svg>).)*?)<\/svg>/gs;
  let flattened = svg;
  while (countSvgTags(flattened) > 1) {
    const next = flattened.replace(nestedSvgPattern, (match, rawAttributes, inner) => {
      if (match === flattened) {
        return match;
      }
      const attributes = attributesFor(rawAttributes);
      const x = numericAttribute(attributes, 'x', 0);
      const y = numericAttribute(attributes, 'y', 0);
      let transform = `translate(${x},${y})`;

      const viewBox = attributes.get('viewBox');
      if (viewBox) {
        const parts = viewBox.trim().split(/[\s,]+/).map(Number);
        const width = numericAttribute(attributes, 'width', Number.NaN);
        const height = numericAttribute(attributes, 'height', Number.NaN);
        if (parts.length === 4
            && parts.every(Number.isFinite)
            && Number.isFinite(width)
            && Number.isFinite(height)
            && parts[2] !== 0
            && parts[3] !== 0) {
          transform += ` scale(${width / parts[2]},${height / parts[3]}) translate(${-parts[0]},${-parts[1]})`;
        }
      }

      return `<g transform="${transform}">${inner}</g>`;
    });
    if (next === flattened) {
      break;
    }
    flattened = next;
  }
  return flattened;
}

function importMathJaxComponent(specifier) {
  if (/^(?:file|data|node):/.test(specifier)) {
    return import(specifier);
  }
  if (path.isAbsolute(specifier) || /^[a-zA-Z]:[\\/]/.test(specifier)) {
    return import(pathToFileURL(specifier).href);
  }
  return import(specifier);
}

try {
  const request = JSON.parse(input || '{}');
  const tex = String(request.tex ?? '');
  const display = Boolean(request.display);
  const color = String(request.color ?? '#e8eaed');

  await MathJax.init({
    loader: { load: ['input/tex', 'output/svg'], require: importMathJaxComponent },
    tex: { packages: ['base', 'ams'] },
    output: { linebreaks: { inline: false } },
    svg: { fontCache: 'none' },
  });

  const node = await MathJax.tex2svgPromise(tex, { display });
  const xml = MathJax.startup.adaptor.serializeXML(node);
  const match = xml.match(/<svg[\s\S]*<\/svg>/);
  if (!match) {
    throw new Error('MathJax did not produce an SVG element');
  }

  const svg = flattenNestedSvg(match[0].replaceAll('currentColor', color));
  process.stdout.write(JSON.stringify({ svg }));
} catch (error) {
  process.stdout.write(JSON.stringify({ error: String(error?.message ?? error) }));
  process.exitCode = 1;
}
