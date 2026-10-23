-- Configuração da janela (lida pelo LOVR antes de carregar o main.lua).
function lovr.conf(t)
  t.identity = 'loomia'
  t.window.title = 'Loomia'
  t.window.width = 1080
  t.window.height = 720
  t.window.vsync = 1
  t.window.msaa = 0
  t.window.resizable = true
  t.modules.headset = false
  t.modules.audio = true
  t.modules.physics = false
  t.versions.gles = '3.0'
end
