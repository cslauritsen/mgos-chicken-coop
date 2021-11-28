function update_ndstatus() {
    $(document).ready(function() {
      $.get("/rpc/cnorthdoor.status", function(data, status){
        cell = $("#ndstatus");
        if (status === "success") {
          cell.text(data.status);
          cell.css("background-color", data.status === "closed" ? "green" : "red");
          $("#nextopen").text(data.sched.next_open_timestamp);
          $("#nextclose").text(data.sched.next_close_timestamp);
          $("#cronenabled").text(data.sched.enabled ? "Yes" : "No");
          $("#now").text(data.current_time);
        }
        else {
          cell.text(status);
          cell.css("background-color", "yellow");
        }
      });
    });  
  }

  function load_data() {
    $(document).ready(function() {

      $.get("/rpc/tempf.read", function(data, status) {
        cell = $("#tempf");
        if (status === "success") {
          cell.text(data.value.toFixed(1) + "°F");
        }
        else {
          cell.text(status);
          cell.css("background-color", "yellow");
        }
      });

      $.get("/rpc/rh.read", function(data, status) {
      cell = $("#rh");
        if (status === "success") {
          cell.text(data.value.toFixed(1) + "%");
        }
        else {
          cell.text(status);
          cell.css("background-color", "yellow");
        }
      });
      update_ndstatus();
    });

    $('#btnopen').click(function() {
      $.get("/rpc/cnorthdoor.open", function(data, status){
        alert("Closing Result: " + data.value + "\nStatus: " + status);
        update_ndsstatus();
      });
      return false;
    });

    $('#btnclose').click(function() {
      $.get("/rpc/cnorthdoor.close", function(data, status){
        alert("Closing Result: " + data.value + "\nStatus: " + status);
        update_ndsstatus();
      });
      return false;
    });

    $('#btnstop').click(function() {
      $.get("/rpc/cnorthdoor.stop", function(data, status){
        alert("Closing Result: " + data.value + "\nStatus: " + status);
        update_ndsstatus();
      });
      return false;
    });
  }