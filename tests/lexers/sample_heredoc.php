<?php
// PHP heredoc and nowdoc lexer sample.
$title = "textlt";

$html = <<<HTML
<section class="hero">
  <style>
    .hero { color: #26a69a; margin: 1rem; }
  </style>
  <h1>{$title}</h1>
  <script>
    const enabled = true;
  </script>
</section>
HTML;

$sql = <<<'SQL'
SELECT id, name
FROM documents
WHERE title = 'textlt';
SQL;
