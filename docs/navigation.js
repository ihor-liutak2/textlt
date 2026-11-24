(() => {
  const nav = document.getElementById("siteNav");
  const currentPage = nav.dataset.page;
  const projectPages = [
    ["history", "History", "history.html"],
    ["architecture", "Architecture", "architecture.html"],
    ["roadmap", "Roadmap", "roadmap.html"],
    ["research", "Research", "research.html"],
  ];

  function pageLink([page, label, href], className = "nav-link") {
    const active = page === currentPage;
    return `<a class="${className}${active ? " active" : ""}"${active ? ' aria-current="page"' : ""} href="${href}">${label}</a>`;
  }

  const projectActive = projectPages.some(([page]) => page === currentPage);
  nav.innerHTML = `
    <div class="container">
      <a class="navbar-brand d-flex align-items-center gap-2 fw-bold" href="./" aria-label="TextLT home">
        <span class="brand-mark" aria-hidden="true">T</span>
        <span>TextLT</span>
      </a>
      <button class="navbar-toggler" type="button" data-bs-toggle="collapse"
              data-bs-target="#mainNav" aria-controls="mainNav" aria-expanded="false"
              aria-label="Toggle navigation">
        <span class="navbar-toggler-icon"></span>
      </button>
      <div class="collapse navbar-collapse" id="mainNav">
        <ul class="navbar-nav ms-auto px-3 px-lg-0 py-2 py-lg-0 gap-lg-2">
          <li class="nav-item">${pageLink(["home", "Home", "./"])}</li>
          <li class="nav-item">${pageLink(["features", "Features", "features.html"])}</li>
          <li class="nav-item">${pageLink(["use-cases", "Use Cases", "use-cases.html"])}</li>
          <li class="nav-item dropdown">
            <button id="projectNav" class="nav-link dropdown-toggle${projectActive ? " active" : ""}"
                    type="button" data-bs-toggle="dropdown" aria-expanded="false" aria-controls="projectNavPages">Project</button>
            <ul id="projectNavPages" class="dropdown-menu" aria-labelledby="projectNav">
              ${projectPages.map(page => `<li>${pageLink(page, "dropdown-item")}</li>`).join("")}
            </ul>
          </li>
          <li class="nav-item">${pageLink(["about", "About", "about.html"])}</li>
          <li class="nav-item"><a class="nav-link" href="https://github.com/ihor-liutak2/textlt">GitHub</a></li>
        </ul>
      </div>
    </div>`;

  // GitHub Pages stages these images under media. A repository preview can use assets directly.
  for (const image of document.querySelectorAll('.screenshot[src^="media/images/"]')) {
    const source = image.getAttribute("src");
    const useSourceAsset = () => {
      if (image.getAttribute("src") === source) {
        image.src = source.replace("media/", "../assets/");
      }
    };
    if (image.complete && !image.naturalWidth) {
      useSourceAsset();
    } else {
      image.addEventListener("error", useSourceAsset, { once: true });
    }
  }
})();
