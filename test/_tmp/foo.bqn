#bar ← •Import "bar.bqn"
#ffi←"./lib.so" •FFI "u32"‿"u32"
abc←↕2
y←1
Fun←{ 𝕊 a‿b: abc+⟜a↩ ⋄ abc+b }
#Foo←{ 𝕊 a: abc+a }

Fun 1‿2
Fun 2‿3

#abc↩1
#abc↩2
#Foo 2‿3

#G a
#G b
#F b

#FFI 1
#FFI 2
#FFI 3
