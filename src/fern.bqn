⟨Import⟩ ← •Import "p.bqn"
conf ← •Import "conf.bqn"
⟨sql⟩  ←s← •Import "s.bqn"
repl   ← •Import "repl.bqn"

flags←⟨
  conf.SetDebug‿"-g"
⟩

Path←{•wdpath∾"/"∾𝕩}
# TODO consume flag args
#{ 𝕊 f‿i: F¨i⊸(⊢/˜∨´∘⍷¨) •args}¨flags

sql.Setup @
Import ⊑Path¨•args

#repl.Run ∾•FLines¨"-i"(⊢/˜⊣⊸»∘∨´∘⍷¨)•args
repl.Run ⟨⟩

