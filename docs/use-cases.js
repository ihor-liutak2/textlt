(() => {
  const examples = document.getElementById("project-examples");
  const picker = document.getElementById("example-project");
  const output = document.getElementById("example-details");

  function showExample() {
    const project = document.getElementById(picker.value);
    const name = project.querySelector(".example-project-name").cloneNode(true);
    const configuration = project.querySelector(".example-project-config").cloneNode(true);
    output.replaceChildren(name, configuration);
  }

  showExample();
  picker.addEventListener("change", showExample);
  examples.hidden = false;
})();
