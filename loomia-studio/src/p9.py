src = open('editor_lua.cpp').read()
# move a declaração do mouse (que está após "LoomScript& script = ...") para antes das abas
old = '''  LoomScript& script = loomDoc_.scripts[static_cast<size_t>(luaEditorScript_)];
  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;
  // Cabeçalho: nome do script, alvo, botões salvar/executar/remover'''
new = '''  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;
  LoomScript& script = loomDoc_.scripts[static_cast<size_t>(luaEditorScript_)];
  // Cabeçalho: nome do script, alvo, botões salvar/executar/remover'''
assert old in src, 'bloco mouse nao achado'
src = src.replace(old, new)
open('editor_lua.cpp','w').write(src)
print('ok')
