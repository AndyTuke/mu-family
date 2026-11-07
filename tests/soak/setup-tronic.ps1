# Fuzz setup for mu-Tronic: dismiss the support reminder, then give slots 1-8 one of each layer type (chooser card positions at 20,20 placement).
Click-Mu $app 473 503 -Settle 800
$adds = @(@(271,285),@(627,285),@(983,285),@(271,460),@(187,645),@(363,645),@(538,645),@(713,645))
$slotY = 115
foreach ($a in $adds) {
    Click-Mu $app 42 $slotY -Settle 900
    Click-Mu $app $a[0] $a[1] -Settle 1800
    $slotY += 80
}
Click-Mu $app 118 44 -Settle 800   # play
