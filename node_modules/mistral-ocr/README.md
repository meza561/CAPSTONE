# mistral-ocr

`mistral-ocr` converts PDFs into Markdown and/or DOCX using Mistral OCR 4 (`mistral-ocr-4-0`) by default.

The project exposes:

- a `mistral-ocr` CLI
- a reusable JavaScript/TypeScript API

## Installation

Use it as an npm package:

```bash
npm install mistral-ocr
```

For local package development:

```bash
npm install
npm run build
```

Required environment variable:

```bash
export MISTRAL_API_KEY=...
```

## Usage CLI

Standard conversion to Markdown + DOCX with image extraction:

```bash
npx mistral-ocr convert ./document.pdf
```

Default outputs:

- `./document.md`
- `./document.docx`
- `./document-images/`

Main options:

```bash
npx mistral-ocr convert ./document.pdf \
  --output-dir ./out \
  --markdown ./out/document.md \
  --docx ./out/document.docx \
  --images-dir ./out/images \
  --model mistral-ocr-4-0
```

Use `--model mistral-ocr-latest` if you explicitly want to follow Mistral's moving latest alias.

Generate Markdown only:

```bash
npx mistral-ocr convert ./document.pdf --no-docx
```

Generate DOCX only:

```bash
npx mistral-ocr convert ./document.pdf --no-markdown
```

Batch OCR conversion:

```bash
npx mistral-ocr batch ./doc-a.pdf ./doc-b.pdf --output-dir ./out
```

Batch mode uses Mistral's Batch Inference endpoint for OCR, waits for the job by default, then writes one Markdown/DOCX pair per input PDF:

- `./out/doc-a.md`
- `./out/doc-a.docx`
- `./out/doc-a-images/`
- `./out/doc-b.md`
- `./out/doc-b.docx`
- `./out/doc-b-images/`

Submit a batch job without waiting for results:

```bash
npx mistral-ocr batch ./doc-a.pdf ./doc-b.pdf --no-wait
```

Useful batch options:

```bash
npx mistral-ocr batch ./doc-a.pdf ./doc-b.pdf \
  --output-dir ./out \
  --poll-interval 10 \
  --timeout 1800 \
  --no-docx
```

OCR 4 options are available in both single-file and batch modes:

```bash
npx mistral-ocr convert ./document.pdf \
  --table-format html \
  --extract-header \
  --extract-footer \
  --image-limit 20 \
  --image-min-size 128
```

Structured document annotations can be requested with JSON response formats:

```bash
npx mistral-ocr convert ./document.pdf \
  --document-annotation-format '{"type":"json_schema","jsonSchema":{"name":"summary","schema":{"type":"object"}}}' \
  --document-annotation-prompt "Extract a compact document summary."
```

## Library Usage

```ts
import { convertPdf } from 'mistral-ocr';

const result = await convertPdf('./document.pdf', {
  markdownPath: './out/document.md',
  docxPath: './out/document.docx',
  imageOutputDir: './out/images',
  tableFormat: 'html',
  extractHeader: true,
  extractFooter: true,
});

console.log(result.markdown);
console.log(result.docxBuffer?.length);
```

Example without writing to disk:

```ts
import { convertPdf } from 'mistral-ocr';

const result = await convertPdf('./document.pdf', {
  generateDocx: false,
  logger: false,
});

console.log(result.markdown);
```

Batch API:

```ts
import { convertPdfBatch, createOcrBatch, waitForOcrBatch } from 'mistral-ocr';

const batch = await convertPdfBatch(['./doc-a.pdf', './doc-b.pdf'], {
  outputDir: './out',
  generateDocx: false,
});

console.log(batch.job.id);
console.log(batch.entries.map((entry) => entry.markdownPath));

const submitted = await createOcrBatch(['./large-a.pdf', './large-b.pdf']);
const finished = await waitForOcrBatch(submitted.job.id);
console.log(finished.status);
```

## Scan-Specific Notes

This library follows the format returned by the Mistral OCR API:

- text is returned as Markdown, page by page
- extracted images are first referenced as placeholders in the OCR Markdown, then remapped to local files when `imageOutputDir` is provided
- DOCX generation is intentionally lightweight and focuses on headings, paragraphs, and images

Practical implications:

- scanned PDFs, multi-column layouts, tables, figures, and captions are generally handled well by `mistral-ocr-4-0`
- OCR 4 adds explicit controls for table output (`markdown` or `html`), header/footer extraction, image extraction limits, and structured document or bounding-box annotations
- complex tables, equations, or very rich layouts remain most faithful in the raw Markdown produced by the model
- DOCX output does not try to perfectly reconstruct the original Word-style layout; it aims to produce a usable document

Official references:

- API OCR Mistral: https://docs.mistral.ai/capabilities/document_ai/basic_ocr/
- Mistral Batch Inference: https://docs.mistral.ai/capabilities/batch/
- Mistral OCR 4 benchmark and release notes: https://mistral.ai/news/ocr-4/

## Exported API

- `convertPdf(input, options)`
- `convertPdfBatch(inputs, options)`
- `createOcrBatch(inputs, options)`
- `waitForOcrBatch(jobId, options)`
- `listOcrBatchOutputs(job, options)`
- `markdownToDocx(markdown, options)`
- `createMistralClient(apiKey?)`
- `buildMarkdownFromOcrResponse(ocrResponse, replacements?)`
- `extractImagesFromOcrResponse(ocrResponse)`
- `writeExtractedImages(images, imageOutputDir, referenceBaseDir?)`

## Development

```bash
npm run build
node build/cli.js --help
```

## Release

Publishing is handled by GitHub Actions through npm Trusted Publishing.

Release flow:

```bash
npm version patch
git push origin master --follow-tags
```

The publish job only runs for `v*` tags. Before publishing, CI verifies that:

- the Git tag matches `package.json` exactly, for example `v0.1.1` for version `0.1.1`
- the package version is not already present on npm
- `npm run verify` passes

## Local Tests

For local testing in this workspace, the Mistral key can be loaded from `../top-ai-ideas-fullstack/.env`.

Recommended test PDF:

- `New York illustrated` (Library of Congress, 1878), 122 illustrated pages, public domain
- source page: https://www.loc.gov/item/01014750/
- direct PDF: https://tile.loc.gov/storage-services/public/gdcmassbookdig/newyorkillustrat03newy/newyorkillustrat03newy.pdf

Useful commands:

```bash
npm run build
mkdir -p .scratch/mistral-ocr-tests
curl -L https://tile.loc.gov/storage-services/public/gdcmassbookdig/newyorkillustrat03newy/newyorkillustrat03newy.pdf -o .scratch/mistral-ocr-tests/new-york-illustrated.pdf

bash -lc 'set -a; source ../top-ai-ideas-fullstack/.env >/dev/null 2>&1; set +a; node build/cli.js convert .scratch/mistral-ocr-tests/new-york-illustrated.pdf --output-dir .scratch/mistral-ocr-tests/new-york-illustrated-out'

bash -lc 'set -a; source ../top-ai-ideas-fullstack/.env >/dev/null 2>&1; set +a; node build/cli.js convert CONTRIBUATION_AI_AERONAUTIQUE.pdf --output-dir .scratch/mistral-ocr-tests/contribution-out'

bash -lc 'set -a; source ../top-ai-ideas-fullstack/.env >/dev/null 2>&1; set +a; node build/cli.js batch CONTRIBUATION_AI_AERONAUTIQUE.pdf .scratch/mistral-ocr-tests/new-york-illustrated.pdf --output-dir .scratch/mistral-ocr-tests/batch-out --no-docx'
```
