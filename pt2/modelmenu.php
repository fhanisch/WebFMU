<?php
  $dir = "../fmu";
  $files = scandir($dir);
  $models = [];
  $j=0;
  $txt = "<select id='modelSelection' onchange='changeSelection()' style='width: 50%'>";
  for ($i=0;$i<sizeof($files);$i++)
  {
    $pos = strpos($files[$i], "modelDescription");
    if ($pos !== false)
    {
      $models[$j] = substr($files[$i],0,-4);
      $txt .= "<option>";
      $txt .= $models[$j];
      $txt .= "</option>";
      $j++;
    }
  }
  $txt .= "</select>";
  echo $txt;
?>
