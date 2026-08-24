import { mathjax } from "mathjax-full/mjs/mathjax.js";
import { TeX } from "mathjax-full/mjs/input/tex.js";
import { SVG } from "mathjax-full/mjs/output/svg.js";
import { liteAdaptor } from "mathjax-full/mjs/adaptors/liteAdaptor.js";
import { RegisterHTMLHandler } from "mathjax-full/mjs/handlers/html.js";

import "mathjax-full/mjs/input/tex/action/ActionConfiguration.js";
import "mathjax-full/mjs/input/tex/ams/AmsConfiguration.js";
import "mathjax-full/mjs/input/tex/amscd/AmsCdConfiguration.js";
import "mathjax-full/mjs/input/tex/bbox/BboxConfiguration.js";
import "mathjax-full/mjs/input/tex/boldsymbol/BoldsymbolConfiguration.js";
import "mathjax-full/mjs/input/tex/braket/BraketConfiguration.js";
import "mathjax-full/mjs/input/tex/cancel/CancelConfiguration.js";
import "mathjax-full/mjs/input/tex/cases/CasesConfiguration.js";
import "mathjax-full/mjs/input/tex/color/ColorConfiguration.js";
import "mathjax-full/mjs/input/tex/enclose/EncloseConfiguration.js";
import "mathjax-full/mjs/input/tex/extpfeil/ExtpfeilConfiguration.js";
import "mathjax-full/mjs/input/tex/html/HtmlConfiguration.js";
import "mathjax-full/mjs/input/tex/mathtools/MathtoolsConfiguration.js";
import "mathjax-full/mjs/input/tex/mhchem/MhchemConfiguration.js";
import "mathjax-full/mjs/input/tex/newcommand/NewcommandConfiguration.js";
import "mathjax-full/mjs/input/tex/noerrors/NoErrorsConfiguration.js";
import "mathjax-full/mjs/input/tex/noundefined/NoUndefinedConfiguration.js";
import "mathjax-full/mjs/input/tex/physics/PhysicsConfiguration.js";
import "mathjax-full/mjs/input/tex/textcomp/TextcompConfiguration.js";
import "mathjax-full/mjs/input/tex/textmacros/TextMacrosConfiguration.js";
import "mathjax-full/mjs/input/tex/unicode/UnicodeConfiguration.js";
import "mathjax-full/mjs/input/tex/verb/VerbConfiguration.js";

import "mathjax-modern-font/mjs/svg/dynamic/accents.js";
import "mathjax-modern-font/mjs/svg/dynamic/accents-b-i.js";
import "mathjax-modern-font/mjs/svg/dynamic/arrows.js";
import "mathjax-modern-font/mjs/svg/dynamic/calligraphic.js";
import "mathjax-modern-font/mjs/svg/dynamic/double-struck.js";
import "mathjax-modern-font/mjs/svg/dynamic/fraktur.js";
import "mathjax-modern-font/mjs/svg/dynamic/latin.js";
import "mathjax-modern-font/mjs/svg/dynamic/latin-b.js";
import "mathjax-modern-font/mjs/svg/dynamic/latin-bi.js";
import "mathjax-modern-font/mjs/svg/dynamic/latin-i.js";
import "mathjax-modern-font/mjs/svg/dynamic/math.js";
import "mathjax-modern-font/mjs/svg/dynamic/monospace.js";
import "mathjax-modern-font/mjs/svg/dynamic/monospace-ex.js";
import "mathjax-modern-font/mjs/svg/dynamic/monospace-l.js";
import "mathjax-modern-font/mjs/svg/dynamic/PUA.js";
import "mathjax-modern-font/mjs/svg/dynamic/sans-serif.js";
import "mathjax-modern-font/mjs/svg/dynamic/sans-serif-b.js";
import "mathjax-modern-font/mjs/svg/dynamic/sans-serif-bi.js";
import "mathjax-modern-font/mjs/svg/dynamic/sans-serif-ex.js";
import "mathjax-modern-font/mjs/svg/dynamic/sans-serif-i.js";
import "mathjax-modern-font/mjs/svg/dynamic/sans-serif-r.js";
import "mathjax-modern-font/mjs/svg/dynamic/script.js";
import "mathjax-modern-font/mjs/svg/dynamic/shapes.js";
import "mathjax-modern-font/mjs/svg/dynamic/symbols.js";
import "mathjax-modern-font/mjs/svg/dynamic/symbols-b-i.js";
import "mathjax-modern-font/mjs/svg/dynamic/variants.js";

mathjax.asyncLoad = () => {};
mathjax.asyncIsSynchronous = true;

const adaptor = liteAdaptor();
RegisterHTMLHandler(adaptor);

const packages = [
  "base", "action", "ams", "amscd", "bbox", "boldsymbol", "braket",
  "cancel", "cases", "color", "enclose", "extpfeil", "html", "mathtools",
  "mhchem", "newcommand", "noerrors", "noundefined", "physics", "textcomp",
  "textmacros", "unicode", "verb",
];

const texInput = new TeX({ packages });
const svgOutput = new SVG({
  fontCache: "local",
  linebreaks: { inline: false },
});
const htmlDoc = mathjax.document("", {
  InputJax: texInput,
  OutputJax: svgOutput,
});

svgOutput.font.loadDynamicFilesSync();

function extractSvg(containerNode) {
  for (const child of adaptor.childNodes(containerNode)) {
    if (adaptor.kind(child) === "svg") {
      return adaptor.serializeXML(child);
    }
  }
  return adaptor.innerHTML(containerNode);
}

function renderMath(latex, display) {
  try {
    const node = htmlDoc.convert(latex, { display, containerWidth: 1e7 });
    return extractSvg(node);
  } catch (error) {
    throw new Error("MathJax render error: " + (error?.message || String(error)));
  }
}

globalThis.render = (latex) => renderMath(latex, true);
globalThis.renderInline = (latex) => renderMath(latex, false);
