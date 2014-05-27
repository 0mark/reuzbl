function go_next() {
	var el = document.querySelector("[rel='next']");
	if (el) { // Wow a developer that knows what he's doing!
                el.style.backgroundColor = '#B9FF00';
                el.style.border = '2px solid #4A6600';
                el.style.color = 'black';
		location = el.href;
	}
	else { // Search from the bottom of the page up for a next link.
		var els = document.getElementsByTagName("a");
		var i = els.length;
		while ((el = els[--i])) {
			if (el.text.search(/\bnext\b|\bmore[\.…]*$|[>»]$/i) > -1) {
                                el.style.backgroundColor = '#B9FF00';
                                el.style.border = '2px solid #4A6600';
                                el.style.color = 'black';
				location = el.href;
				break;
			}
		}
	}
}

function go_prev() {
	var el = document.querySelector("[rel='prev']");
	if (el) {
                el.style.backgroundColor = '#B9FF00';
                el.style.border = '2px solid #4A6600';
                el.style.color = 'black';
		location = el.href;
	}
	else {
		var els = document.getElementsByTagName("a");
		var i = els.length;
		while ((el = els[--i])) {
			if (el.text.search(/\bprev|^[<«]/i) > -1) {
                                el.style.backgroundColor = '#B9FF00';
                                el.style.border = '2px solid #4A6600';
                                el.style.color = 'black';
				location = el.href;
				break;
			}
		}
	}
}

function go(where) {
  var d = document.createElement("div");
  d.style.backgroundColor = '#B9FF00';
  d.style.border = '2px solid #4A6600';
  d.style.color = 'black';
  d.style.fontSize = '12px';
  d.style.fontWeight = 'bold';
  d.style.lineHeight = '12px';
  d.style.margin = '0px';
  d.style.padding = '4px';
  d.style.position = 'absolute';
  d.style.width = '100px';
  d.style.textAlign = 'center';
  d.style.left = ((window.innerWidth/2)-112) + 'px';
  d.style.top =  ((window.innerHeight/2)-24) + 'px';
  var t = document.createTextNode(where);
  d.appendChild(t);
  document.getElementsByTagName("body")[0].appendChild(d);

  if (where=="next") go_next();
  if (where=="prev") go_prev();
}

go("%s");