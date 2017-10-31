<?php
  $name = $_GET["n"];
  $tau = $_GET["T"];
  echo "huhu";
  //echo "<div id='myPlot'><!-- Plotly chart will be drawn inside this DIV --></div><script>function createPlot(){var trace1 = {x: [1, 2, 3, 4],y: [10, 15, 13, 17],type: 'scatter'};var trace2 = {x: [1, 2, 3, 4],y: [16, 5, 11, 9],type: 'scatter'};var data = [trace1, trace2];Plotly.newPlot('myPlot', data)};</script>";
  simulate($name, $tau);
  echo "<button type='button' onclick='plot_table()'>Change Content</button>";
  echo "<div id='table'></div>";
  echo my_function();
?>
