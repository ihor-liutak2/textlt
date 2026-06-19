// JavaScript lexer sample
import { render } from "./view.js";

const enabled = true;
function start(port = 8080) {
  return render(`port=${port}`);
}
