src = open('editor_lua.cpp').read()
# move a declaração de mx/my/down/clicked para antes das abas
old = '''  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  // Alvo: alterna server/client/both'''
assert old in src
src = src.replace(old, '''  // Alvo: alterna server/client/both''')

old2 = '''  // ===== abas dos scripts =====
  {'''
new2 = '''  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  // ===== abas dos scripts =====
  {'''
assert old2 in src
src = src.replace(old2, new2)
open('editor_lua.cpp','w').write(src)
print('ok')
